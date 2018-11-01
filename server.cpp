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
    char name[Player::NameBufSize] = "dummy";
    int sockfd;
    bool remove = false;
    bool alive = true;
};

struct Bot
{
    char name[Player::NameBufSize];
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

bool wouldBlock()
{
    return errno == EAGAIN || errno == EWOULDBLOCK;
}

enum {MaxClients = 10};

void sendInitTileData(FixedArray<Client, MaxClients>& clients, Array<char>* sendBufs, int* tileMap)
{
    for(int cidx = 0; cidx < clients.size(); ++cidx)
    {
        if(clients[cidx].status == ClientStatus::InGame)
        {
            constexpr int numTiles = Simulation::MapSize * Simulation::MapSize;

            char buf[numTiles * 2]; // for each value we add one space
            char* it = buf;
            
            for(int i = 0; i < numTiles; ++i)
            {
                *it = tileMap[i] + 48; // converting to ascii
                ++it;
                *it = ' ';
                ++it;
            }

            addMsg(sendBufs[cidx], Cmd::InitTileData, buf);
        }
    }
}

static volatile int gExitLoop = false;
void sigHandler(int) {gExitLoop = true;}

void setNewGame(FixedArray<Client, MaxClients>& clients, const FixedArray<Bot, MaxPlayers>& bots,
        Simulation& sim)
{
    sim.players_.clear();

    for(const Client& client: clients)
    {
        if(client.status == ClientStatus::InGame)
        {
            sim.players_.pushBack({});
            memcpy(sim.players_.back().name, client.name, Player::NameBufSize);
        }
    }

    for(const Bot& bot: bots)
    {
        sim.players_.pushBack({});
        memcpy(sim.players_.back().name, bot.name, Player::NameBufSize);
    }

    sim.setNewGame();
}

bool nameAvailable(const FixedArray<Client, MaxClients>& clients,
        const FixedArray<Bot, MaxPlayers>& bots, const char* name)
{
    for(const Client& client: clients)
    { 
        if(client.status == ClientStatus::InGame && strcmp(client.name, name) == 0)
            return false;
    }

    for(const Bot& bot: bots)
    { 
        if(strcmp(bot.name, name) == 0)
            return false;
    }

    return true;
}

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

    FixedArray<Client, MaxClients> clients;
    Array<char> sendBufs[MaxClients];
    Array<char> recvBufs[MaxClients];
    int recvBufsNumUsed[MaxClients];

    for(int i = 0; i < MaxClients; ++i)
    {
        sendBufs[i].reserve(500);
        recvBufs[i].resize(500);
    }

    double currentTime = getTimeSec();
    float timer = 0.f;

    Simulation sim;
    FixedArray<ExploEvent, 50> exploEvents;
    FixedArray<Bot, MaxPlayers> bots;

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
                        printf("client %s (%s) will be removed (no PONG or init msg)\n",
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
                // this can't be combined with the parent if
                if(!wouldBlock())
                {
                    perror("accept()");
                    assert(false);
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
                    // this can't be combined with the parent if
                    if(!wouldBlock())
                    {
                        perror("recv() failed");
                        client.remove = true;
                    }

                    break;
                }
                else if(rc == 0)
                {
                    printf("%s (%s) has closed the connection\n", client.name,
                            getStatusStr(client.status));
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
                        printf("recvBuf BIG SIZE ISSUE, clearing the buffer for %s (%s)\n",
                                client.name, getStatusStr(client.status));

                        recvBufNumUsed = 0;
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
                        printf("%s (%s) WARNING unknown command received: %s\n", thisClient.name,
                                getStatusStr(thisClient.status), begin);
                        break;

                    case Cmd::Ping:
                        addMsg(sendBuf, Cmd::Pong);
                        break;

                    case Cmd::Pong:
                        thisClient.alive = true;
                        break;

                    case Cmd::SetName:
                    {
                        // validate if there is no whitespace
                        // otherwise our serialization system will fail :D
                        {
                            bool valid = true;
                            const char* str = begin;
                            while(*str != '\0')
                            {
                                const char code = *str;

                                if(code < 33 || code > 126)
                                {
                                    valid = false;
                                    break;
                                }

                                ++str;
                            }

                            if(!valid)
                            {
                                addMsg(sendBuf, Cmd::MustRename, begin);
                                break;
                            }
                        }

                        const bool isInGame = thisClient.status == ClientStatus::InGame;

                        // case when in-game client sets the same name once again
                        if(isInGame)
                        {
                            if(strcmp(thisClient.name, begin) == 0)
                                break;
                        }

                        if(!isInGame)
                            thisClient.status = ClientStatus::Lobby;

                        if(!isInGame)
                        {
                            if(sim.players_.size() == sim.players_.maxSize())
                            {
                                addMsg(sendBuf, Cmd::GameFull);
                                break;
                            }
                        }

                        if(!nameAvailable(clients, bots, begin))
                        {
                            addMsg(sendBuf, Cmd::MustRename, begin);
                            break;
                        }

                        bool rename = false;
                        char oldName[Player::NameBufSize];

                        if(isInGame)
                        {
                            rename = true;
                            memcpy(oldName, thisClient.name, Player::NameBufSize);
                        }
                        else
                            thisClient.status = ClientStatus::InGame;

                        memcpy(thisClient.name, begin, Player::NameBufSize);
                        thisClient.name[Player::NameBufSize - 1] = '\0';
                        addMsg(sendBuf, Cmd::NameOk, thisClient.name);

                        // send announcement
                        for(int i = 0; i < clients.size(); ++i)
                        {
                            if(clients[i].status == ClientStatus::InGame)
                            {
                                char msg[128];

                                if(!rename)
                                    snprintf(msg, sizeof(msg), "%s has joined the game!",
                                             thisClient.name);
                                else
                                    snprintf(msg, sizeof(msg), "%s changed name to %s!",
                                             oldName, thisClient.name);

                                addMsg(sendBufs[i], Cmd::Chat, msg);
                            }
                        }

                        setNewGame(clients, bots, sim);
                        sendInitTileData(clients, sendBufs, sim.tiles_[0]);
                        break;
                    }

                    case Cmd::Chat:
                    {
                        if(thisClient.status != ClientStatus::InGame)
                        {
                            printf("WARNING %s (%s) tried to send chat msg but is not in game\n",
                                    thisClient.name, getStatusStr(thisClient.status));
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

                        // @TODO remove this assert...
                        assert(sscanf(begin, "%d %d %d %d %d", &action.up, &action.down,
                                    &action.left, &action.right, &action.drop) == 5);

                        sim.processPlayerInput(action, thisClient.name);
                        break;
                    }

                    // @TODO send operation status to clients
                    case Cmd::AddBot:
                    {
                        if(sim.players_.size() == sim.players_.maxSize())
                            break;

                        Bot bot;
                        int idx = 0;
                        while(true)
                        {
                            snprintf(bot.name, sizeof(bot.name), "bot_%d", idx++);

                            if(nameAvailable(clients, bots, bot.name))
                            {
                                bots.pushBack(bot);
                                setNewGame(clients, bots, sim);
                                sendInitTileData(clients, sendBufs, sim.tiles_[0]);
                                break;
                            }
                        }

                        break;
                    }

                    // @TODO send operation status to clients
                    case Cmd::RemoveBot:
                    {
                        if(bots.size())
                        {
                            bots.popBack();
                            setNewGame(clients, bots, sim);
                            sendInitTileData(clients, sendBufs, sim.tiles_[0]);
                        }

                        break;
                    }
                }
            }

            const int numToFree = end - recvBuf.data();
            memmove(recvBuf.data(), recvBuf.data() + numToFree, recvBufNumUsed - numToFree);
            recvBufNumUsed -= numToFree;
        }

        // run the simulation
        {
            // we check clients and not sim.players_ because we don't want to update simulation
            // if only bots are playing

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

                for(const Bot& bot: bots)
                    sim.updateAndProcessBotInput(bot.name, dt);

                if(sim.update(dt, exploEvents))
                {
                    sendInitTileData(clients, sendBufs, sim.tiles_[0]);
                }

                // protocol:
                // - time to start
                // - num players
                // - data for each player (name must not contain any white characters)
                // - num bombs
                // - data for each bomb
                // - num explo events
                // - data for each explo event
                // 
                // client can update the tiles based on exploEvents

                char buf[2048]; // hope it is enough :DDD
                int offset = 0;

                const int numPlayers = sim.players_.size();

                offset += sprintf(buf, "%f %d ", sim.timeToStart_, numPlayers);


                for(int i = 0; i < numPlayers; ++i)
                {
                    const Player& p = sim.players_[i];
                    offset += sprintf(buf + offset, "%f %f %f %d %f %d %d %s %f %d ",
                            p.pos.x, p.pos.y, p.vel, p.dir, p.dropCooldown, p.hp, p.score,
                            p.name, p.dmgTimer, p.prevDir);
                }

                offset += sprintf(buf + offset, "%d ", sim.bombs_.size());

                for(const Bomb& b: sim.bombs_)
                {
                    offset += sprintf(buf + offset, "%d %d %d %f %d %d ",
                            b.tile.x, b.tile.y, b.range, b.timer, b.playerIdxs[0],
                            b.playerIdxs[1]);
                }

                offset += sprintf(buf + offset, "%d ", exploEvents.size());

                for(const ExploEvent& e: exploEvents)
                {
                    offset += sprintf(buf + offset, "%d %d %d ", e.tile.x, e.tile.y, e.type);
                }

                const int numBytes = offset + 1; // null char
                int percentOccupied = int(float(numBytes) / sizeof(buf) * 100.f);

                if(percentOccupied > 50)
                {
                    printf("SIMULATION: %d bytes used (%d%%), %d available\n", numBytes,
                            percentOccupied, int(sizeof(buf)));
                }

                for(int i = 0; i < clients.size(); ++i)
                {
                    if(clients[i].status == ClientStatus::InGame)
                        addMsg(sendBufs[i], Cmd::Simulation, buf);
                }
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
                    // this can't be combined with the parent if
                    if(!wouldBlock())
                    {
                        perror("send() failed");
                        clients[i].remove = true;
                    }
                }
                else
                    buf.erase(0, rc);
            }
        }

        bool needSetNewGame = false;

        // remove some clients
        for(int cidx = 0; cidx < clients.size(); ++cidx)
        {
            Client& client = clients[cidx];
            if(client.remove || client.status == ClientStatus::Browser)
            {
                // inform other players if someone will leave the game
                if(client.status == ClientStatus::InGame)
                {
                    char buf[64];
                    snprintf(buf, sizeof(buf), "%s has left", client.name);

                    for(int i = 0; i < clients.size(); ++i)
                    {
                        if(!clients[i].remove && clients[i].status == ClientStatus::InGame)
                            addMsg(sendBufs[i], Cmd::Chat, buf);
                    }

                    needSetNewGame = true;
                }

                printf("removing client %s (%s)\n", client.name, getStatusStr(client.status));
                close(client.sockfd);
                client = clients.back();

                const int lastIdx = clients.size() - 1;
                sendBufs[cidx].swap(sendBufs[lastIdx]);
                recvBufs[cidx].swap(recvBufs[lastIdx]);
                recvBufsNumUsed[cidx] = recvBufsNumUsed[lastIdx];

                clients.popBack();
                --cidx;
            }
        }

        if(needSetNewGame)
        {
            setNewGame(clients, bots, sim);
            sendInitTileData(clients, sendBufs, sim.tiles_[0]);
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
