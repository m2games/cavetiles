#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <netinet/tcp.h>
#include "Array.hpp"
#include "Scene.hpp"
#include "Simulation.cpp"

using namespace netcode;

// code duplication with main.cpp
int getRandomInt(const int min, const int max)
{
    assert(min <= max);
    return min + rand() / (RAND_MAX / (max - min + 1) + 1);
}

double getTimeSec()
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1000000000.0;
}

enum class ClientStatus
{
    WaitingForInit,
    Browser,
    InGame,
    Lobby // failed to set his name or the game is full
};

struct Client
{
    ClientStatus status = ClientStatus::WaitingForInit;
    char name[20] = "dummy";
    int sockfd;
    bool remove = false;
    bool alive = true;
};

const char* getStatusStr(ClientStatus code)
{
    switch(code)
    {
        case ClientStatus::WaitingForInit: return "WaitingForInit";
        case ClientStatus::Browser: return "Browser";
        case ClientStatus::InGame:  return "InGame";
        case ClientStatus::Lobby: return "Lobby";
    }
    assert(false);
}

static volatile int gExitLoop = false;
void sigHandler(int) {gExitLoop = true;}

int main()
{
    srand(time(nullptr));

    signal(SIGINT, sigHandler);
    signal(SIGTERM, sigHandler);

    addrinfo hints = {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    addrinfo* list;
    {
        // this blocks
        const int ec = getaddrinfo(nullptr, "3000", &hints, &list);
        if(ec != 0)
        {
            printf("getaddrinfo() failed: %s\n", gai_strerror(ec));
            return 0;
        }
    }

    int sockfd;

    const addrinfo* it;
    for(it = list; it != nullptr; it = it->ai_next)
    {
        sockfd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if(sockfd == -1)
        {
            perror("socket() failed");
            continue;
        }

        const int option = 1;
        if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option)) == -1)
        {
            close(sockfd);
            perror("setsockopt() (SO_REUSEADDR) failed");
            return 0;
        }

        if(fcntl(sockfd, F_SETFL, O_NONBLOCK) == -1)
        {
            close(sockfd);
            perror("fcntl() failed");
            return 0;
        }

        if(bind(sockfd, it->ai_addr, it->ai_addrlen) == -1)
        {
            close(sockfd);
            perror("bind() failed");
            continue;
        }

        break;
    }
    freeaddrinfo(list);

    if(it == nullptr)
    {
        printf("binding procedure failed\n");
        return 0;
    }

    if(listen(sockfd, 5) == -1)
    {
        perror("listen() failed");
        close(sockfd);
        return 0;
    }

    constexpr int maxClients = 10;
    FixedArray<Client, maxClients> clients;
    Array<char> sendBufs[maxClients];
    Array<char> recvBufs[maxClients];
    int recvBufsNumUsed[maxClients];

    for(int i = 0; i < maxClients; ++i)
    {
        sendBufs[i].reserve(500);
        recvBufs[i].resize(500);
    }

    double currentTime = getTimeSec();
    float timer = 0.f;

    Simulation sim;
    FixedArray<ExploEvent, 50> exploEvents;

    // server loop
    // note: don't change the order of operations
    // (some logic is based on this)
    while(gExitLoop == false)
    {
        const double newTime = getTimeSec();
        const double dt = newTime - currentTime;
        timer += dt;
        currentTime = newTime;

        // update clients
        {
            if(timer > 5.f)
            {
                timer = 0.f;

                for(int i = 0; i < clients.size(); ++i)
                {
                    Client& client = clients[i];

                    if(client.alive == false)
                    {
                        printf("client '%s' (%s) will be removed (no PONG or init msg)\n",
                               client.name, getStatusStr(client.status));
                        client.remove = true;
                    }
                    else if(client.status != ClientStatus::WaitingForInit)
                        addMsg(sendBufs[i], Cmd::Ping);

                    client.alive = false;
                }
            }
        }

        // handle new client
        if(clients.size() < clients.maxSize())
        {
            sockaddr_storage clientAddr;
            socklen_t clientAddrSize = sizeof(clientAddr);
            const int clientSockfd = accept(sockfd, (sockaddr*)&clientAddr, &clientAddrSize);

            if(clientSockfd == -1)
            {
                if(errno != EAGAIN || errno != EWOULDBLOCK)
                {
                    perror("accept()");
                    break;
                }
            }
            else
            {
                const int option = 1;

                if(fcntl(clientSockfd, F_SETFL, O_NONBLOCK) == -1)
                {
                    close(clientSockfd);
                    perror("fcntl() on client failed");
                }
                else if(setsockopt(clientSockfd, IPPROTO_TCP, TCP_NODELAY, &option,
                                   sizeof(option)) == -1)
                {
                    close(clientSockfd);
                    perror("setsockopt() (TCP_NODELAY) on client failed");
                }
                else
                {
                    clients.pushBack(Client());
                    clients.back().sockfd = clientSockfd;
                    sendBufs[clients.size() - 1].clear();
                    recvBufsNumUsed[clients.size() - 1] = 0;

                    // print client ip
                    char ipStr[INET6_ADDRSTRLEN];
                    inet_ntop(clientAddr.ss_family, get_in_addr( (sockaddr*)&clientAddr ),
                              ipStr, sizeof(ipStr));
                    printf("accepted connection from %s\n", ipStr);
                }

            }
        }

        // receive
        for(int i = 0; i < clients.size(); ++i)
        {
            Array<char>& recvBuf = recvBufs[i];
            int& recvBufNumUsed = recvBufsNumUsed[i];
            Client& client = clients[i];

            while(true)
            {
                const int numFree = recvBuf.size() - recvBufNumUsed;
                const int rc = recv(client.sockfd, recvBuf.data() + recvBufNumUsed,
                                    numFree, 0);

                if(rc == -1)
                {
                    if(errno != EAGAIN || errno != EWOULDBLOCK)
                    {
                        perror("recv() failed");
                        client.remove = true;
                    }
                    break;
                }
                else if(rc == 0)
                {
                    printf("client has closed the connection\n");
                    client.remove = true;
                    break;
                }
                else
                {
                    recvBufNumUsed += rc;

                    if(recvBufNumUsed < recvBuf.size())
                        break;

                    recvBuf.resize(recvBuf.size() * 2);
                    if(recvBuf.size() > 10000)
                    {
                        printf("recvBuf big size issue, removing client: '%s' (%s)\n",
                               client.name, getStatusStr(client.status));
                        client.remove = true;
                        break;
                    }
                }
            }
        }

        // process received data
        for(int i = 0; i < clients.size(); ++i)
        {
            Array<char>& sendBuf = sendBufs[i];
            Array<char>& recvBuf = recvBufs[i];
            int& recvBufNumUsed = recvBufsNumUsed[i];
            Client& thisClient = clients[i];

            const char* end = recvBuf.data();
            const char* begin;

            // special case for http
            if(recvBufNumUsed >= 3)
            {
                const char* const cmd = "GET";
                if(strncmp(cmd, recvBuf.data(), strlen(cmd)) == 0)
                {
                    thisClient.status = ClientStatus::Browser;
                    addMsg(sendBuf, Cmd::_nil,
                            "HTTP/1.1 200 OK\r\n"
                            "Content-Type: text/html\r\n\r\n"
                            "<!DOCTYPE html>"
                            "<html>"
                            "<body>"
                            "<h1>Welcome to the cavetiles server!</h1>"
                            "<p><a href=\"https://github.com/m2games\">company</a></p>"
                            "</body>"
                            "</html>");
                    continue;
                }
            }

            while(true)
            {
                begin = end;
                {
                    const char* tmp = (const char*)memchr((const void*)end, '\0',
                                       recvBuf.data() + recvBufNumUsed - end);

                    if(tmp == nullptr) break;
                    end = tmp;
                }

                ++end;

                printf("'%s' (%s) received msg: '%s'\n", thisClient.name,
                                                         getStatusStr(thisClient.status), begin);

                int cmd = 0;
                for(int i = 1; i < Cmd::_count; ++i)
                {
                    const char* const cmdStr = getCmdStr(i);
                    const int cmdLen = strlen(cmdStr);

                    if(cmdLen > int(strlen(begin)))
                        continue;

                    if(strncmp(begin, cmdStr, cmdLen) == 0)
                    {
                        cmd = i;
                        begin += cmdLen + 1; // ' '
                        break;
                    }
                }

                switch(cmd)
                {
                    case 0:
                        printf("WARNING unknown command received: '%s'\n", begin);
                        break;

                    case Cmd::Ping:
                        addMsg(sendBuf, Cmd::Pong);
                        break;

                    case Cmd::Pong:
                        thisClient.alive = true;
                        break;

                    case Cmd::SetName:
                    {
                        const bool isInGame = thisClient.status == ClientStatus::InGame;

                        // ...
                        // case when in-game client sets the same name once again
                        if(isInGame)
                        {
                            if(strcmp(thisClient.name, begin) == 0)
                                break;
                        }

                        if(!isInGame)
                            thisClient.status = ClientStatus::Lobby;

                        // check if the game is not full
                        if(!isInGame)
                        {
                            int numPlayers = 0;
                            for(const Client& client: clients)
                            {
                                if(client.status == ClientStatus::InGame)
                                    ++numPlayers;
                            }

                            if(numPlayers == 2)
                            {
                                addMsg(sendBuf, Cmd::GameFull);
                                break;
                            }
                        }

                        // check if the name is available

                        bool ok = true;

                        for(const Client& client: clients)
                        { 
                            if(client.status == ClientStatus::InGame &&
                               strcmp(client.name, begin) == 0)
                            {
                                ok = false;
                                break;
                            }
                        }

                        if(ok)
                        {
                            bool rename = false;
                            char oldName[20];

                            if(isInGame)
                            {
                                rename = true;
                                memcpy(oldName, thisClient.name, 20);
                            }
                            else
                            {
                                thisClient.status = ClientStatus::InGame;
                                sim.setNewGame();
                            }

                            const int maxSize = sizeof(thisClient.name);

                            // + 1 so the null char will be included
                            memcpy(thisClient.name, begin, min(maxSize, int(strlen(begin)) + 1));
                            thisClient.name[maxSize - 1] = '\0';
                            addMsg(sendBuf, Cmd::NameOk, thisClient.name);

                            // update the player name in the simulation
                            for(Player& player: sim.players_)
                            {
                                bool update = true;

                                for(const Client& client: clients)
                                {
                                    if(strcmp(client.name, player.name) == 0)
                                    {
                                        update = false;
                                        break;
                                    }
                                }

                                if(update)
                                {
                                    memcpy(player.name, thisClient.name, sizeof(thisClient.name));
                                    break;
                                }
                            }

                            for(int i = 0; i < clients.size(); ++i)
                            {
                                if(clients[i].status == ClientStatus::InGame)
                                {
                                    char msg[128];

                                    if(!rename)
                                        snprintf(msg, sizeof(msg), "'%s' has joined the game!",
                                                 thisClient.name);
                                    else
                                        snprintf(msg, sizeof(msg), "'%s' changed name to '%s'!",
                                                 oldName, thisClient.name);

                                    addMsg(sendBufs[i], Cmd::Chat, msg);
                                }
                            }
                        }
                        else
                            addMsg(sendBuf, Cmd::MustRename, begin);

                        break;
                    }

                    case Cmd::Chat:
                    {
                        if(thisClient.status != ClientStatus::InGame)
                        {
                            printf("only InGame clients can send chat messages\n");
                            break;
                        }

                        for(int i = 0; i < clients.size(); ++i)
                        {
                            if(clients[i].status == ClientStatus::InGame)
                            {
                                char msg[512];
                                snprintf(msg, sizeof(msg), "%s: %s", thisClient.name, begin);
                                addMsg(sendBufs[i], Cmd::Chat, msg);
                            }
                        }
                        break;
                    }
                    case Cmd::PlayerInput:
                    {
                        Action action;

                        assert(sscanf(begin, "%d %d %d %d %d", &action.up, &action.down,
                                    &action.left, &action.right, &action.drop) == 5);

                        sim.processPlayerInput(action, thisClient.name);
                        break;
                    }
                }
            }

            const int numToFree = end - recvBuf.data();
            memmove(recvBuf.data(), recvBuf.data() + numToFree, recvBufNumUsed - numToFree);
            recvBufNumUsed -= numToFree;
        }

        // inform players if someone will leave the game
        for(const Client& client: clients)
        {
            if(client.remove && client.status == ClientStatus::InGame)
            {
                char buf[64];
                snprintf(buf, sizeof(buf), "'%s' has left", client.name);

                for(int i = 0; i < clients.size(); ++i)
                {
                    if(clients[i].status == ClientStatus::InGame && !clients[i].remove)
                    {
                        addMsg(sendBufs[i], Cmd::Chat, buf);
                    }
                }
            }
        }

        // run the simulation
        {
            bool doSim = false;
            for(const Client& client: clients)
            {
                if(client.status == ClientStatus::InGame)
                {
                    doSim = true;
                    break;
                }
            }

            if(doSim)
            {
                exploEvents.clear();
                sim.update(dt, exploEvents);
                // now send the simulation and exploEvents
            }
        }

        // send
        for(int i = 0; i < clients.size(); ++i)
        {
            if(clients[i].remove)
                continue;

            Array<char>& buf = sendBufs[i];
            if(buf.size())
            {
                const int rc = send(clients[i].sockfd, buf.data(), buf.size(), 0);

                if(rc == -1)
                {
                    perror("send() failed");
                    clients[i].remove = true;
                }
                else
                    buf.erase(0, rc);
            }
        }

        // remove some clients
        for(int i = 0; i < clients.size(); ++i)
        {
            Client& client = clients[i];
            if(client.remove || client.status == ClientStatus::Browser)
            {
                printf("removing client '%s' (%s)\n", client.name,
                       getStatusStr(client.status));

                close(client.sockfd);

                client = clients.back();

                const int lastIdx = clients.size() - 1;
                sendBufs[i].swap(sendBufs[lastIdx]);
                recvBufs[i].swap(recvBufs[lastIdx]);
                recvBufsNumUsed[i] = recvBufsNumUsed[lastIdx];

                clients.popBack();
                --i;
            }
        }

        // @TODO:
        // sleep for 4 ms
        usleep(4000);
    }
    
    for(Client& client: clients)
        close(client.sockfd);

    close(sockfd);
    printf("end of the main function\n");
    return 0;
}
