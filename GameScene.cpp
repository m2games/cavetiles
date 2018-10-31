#include "Scene.hpp"
#include "imgui/imgui.h"
#include "GLFW/glfw3.h"
#include <stdio.h>
#include <stdarg.h>

// includes for the netcode
#include <sys/types.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <netinet/tcp.h>

namespace netcode
{

void log(Array<char>& buf, const char* fmt, ...)
{
    static char bigbuf[1024];

    va_list args;
    va_start(args, fmt);
    vsnprintf(bigbuf, (sizeof bigbuf) - 1, fmt, args);
    va_end(args);

    int len = strlen(bigbuf);
    bigbuf[len++] = '\n';

    const int prevSize = buf.size();
    buf.resize(len + prevSize);
    memmove(buf.begin() + len, buf.begin(), prevSize);
    memcpy(buf.begin(), bigbuf, len);

    if(buf.size() > 9000)
    {
        buf.resize(9000);
        buf.back() = '\0';
    }
}

// returns socket descriptior, -1 if failed
// if succeeded you have to free the socket yourself
// @TODO: this must be non-blocking connect
int connect(Array<char>& logBuf, const char* host)
{
    addrinfo hints = {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* list;
    {
        // this block (domain name resolution)
        const int ec = getaddrinfo(host, "3000", &hints, &list);
        if(ec != 0)
        {
            log(logBuf, "getaddrinfo() failed: %s", gai_strerror(ec));
            return -1;
        }
    }

    int sockfd;

    const addrinfo* it;
    for(it = list; it != nullptr; it = it->ai_next)
    {
        sockfd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if(sockfd == -1)
        {
            log(logBuf, "socket() failed: %s", strerror(errno));
            continue;
        }

        if(connect(sockfd, it->ai_addr, it->ai_addrlen) == -1)
        {
            close(sockfd);
            log(logBuf, "connect() failed: %s", strerror(errno));
            continue;
        }

        char name[INET6_ADDRSTRLEN];
        inet_ntop(it->ai_family, get_in_addr(it->ai_addr), name, sizeof(name));

        log(logBuf, "connected to %s", name);
        break;
    }
    freeaddrinfo(list);

    if(it == nullptr)
    {
        log(logBuf, "connection procedure failed");
        return -1;
    }

    if(fcntl(sockfd, F_SETFL, O_NONBLOCK) == -1)
    {
        close(sockfd);
        log(logBuf, "fcntl() failed: %s", strerror(errno));
        return -1;
    }

    {
        const int option = 1;
        if(setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &option, sizeof(option)) == -1)
        {
            close(sockfd);
            log(logBuf, "setsockopt() (TCP_NODELAY) failed: %s", strerror(errno));
            return -1;
        }
    }

    return sockfd;
}

NetClient::NetClient()
{
    sendBuf.reserve(500);
    recvBuf.resize(500);
    logBuf.reserve(10000);
    logBuf.pushBack('\0'); // terminate with null

    // so we have valid data to display during the time when inGame is set to
    // true but no Simulation data arrived from the server yet
    sim.setNewGame(); 
}

NetClient::~NetClient()
{
    if(sockfd != -1)
        close(sockfd);
}

// count - move by this many words
void gotoNextWord(const char** buf, int count)
{
    for(int i = 0; i < count; ++i)
    {
        while(**buf != ' ')
            ++(*buf);

        ++(*buf);
    }
}

void NetClient::update(const float dt, const char* name, FixedArray<ExploEvent, 50>& exploEvents,
                    Action& playerAction)
{
    if(inGame)
    {
        char buf[10] = {};

        sprintf(buf, "%d %d %d %d %d", playerAction.up, playerAction.down, playerAction.left,
                playerAction.right, playerAction.drop);

        addMsg(sendBuf, Cmd::PlayerInput, buf);
    }

    // time managment
    timerAlive += dt;
    timerReconnect += dt;
    timerSendSetNameMsg += dt;

    if(hasToReconnect)
    {
        inGame = false;

        if(timerReconnect >= timerReconnectMax)
        {
            timerReconnect = 0.f;

            if(sockfd != -1)
                close(sockfd);

            assert(strlen(host));
            sockfd = connect(logBuf, host);

            if(sockfd != -1)
            {
                serverAlive = true;
                timerAlive = timerAliveMax;
                hasToReconnect = false;
                sendBuf.clear();
                sendSetNameMsg = true;
            }
        }
    }

    if(sendSetNameMsg && (timerSendSetNameMsg >= timerReconnectMax))
    {
        sendSetNameMsg = false;
        timerSendSetNameMsg = 0.f;

        int len = strlen(name);
        assert(len < maxNameSize);
        assert(len);
        addMsg(sendBuf, Cmd::SetName, name);
    }

    // update
    if(!hasToReconnect)
    {
        if(timerAlive > timerAliveMax)
        {
            timerAlive = 0.f;

            if(serverAlive)
            {
                serverAlive = false;
                addMsg(sendBuf, Cmd::Ping);
            }
            else
            {
                hasToReconnect = true;
                log(logBuf, "no PONG response from server, will try to reconnect\n");
            }
        }
    }

    // receive
    if(!hasToReconnect)
    {
        while(true)
        {
            const int numFree = recvBuf.size() - recvBufNumUsed;
            const int rc = recv(sockfd, recvBuf.data() + recvBufNumUsed, numFree, 0);

            if(rc == -1)
            {
                if(errno != EAGAIN || errno != EWOULDBLOCK)
                {
                    log(logBuf, "recv() failed: %s", strerror(errno));
                    hasToReconnect = true;
                }

                break;
            }
            else if(rc == 0)
            {
                log(logBuf, "server has closed the connection\n");
                hasToReconnect = true;
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
                    log(logBuf, "recvBuf BIG SIZE ISSUE, clearing the buffer\n");
                    recvBufNumUsed = 0;
                }
            }
        }
    }

    bool newGame = false;

    // process received data
    {
        const char* end = recvBuf.data();
        const char* begin;

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

            //printf("received msg: '%s'\n", begin);

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
                    log(logBuf, "WARNING unknown command received: %s", begin);
                    break;

                case Cmd::Ping:
                    addMsg(sendBuf, Cmd::Pong);
                    break;

                case Cmd::Pong:
                    serverAlive = true;
                    break;

                case Cmd::Chat:
                    log(logBuf, begin);
                    break;

                case Cmd::GameFull:
                {
                    log(logBuf, "%s", getCmdStr(cmd));
                    sendSetNameMsg = true;
                    break;
                }

                case Cmd::NameOk:
                {
                    log(logBuf, "%s %s", getCmdStr(cmd), begin);
                    inGame = true;
                    int len = strlen(begin);
                    assert(len <= maxNameSize);
                    memcpy(inGameName, begin, len);
                    inGameName[len] = '\0';
                    break;
                }

                case Cmd::MustRename:
                {
                    if(inGame)
                        log(logBuf, "%s, could not rename to %s", getCmdStr(cmd), begin);

                    else
                    {
                        log(logBuf, "%s, could not join as %s", getCmdStr(cmd), begin);
                        sendSetNameMsg = true;
                    }

                    break;
                }
                case Cmd::Simulation:
                {
                    // @ !!! we are not validating the data

                    const char* buf = begin;

                    sscanf(buf, "%f", &sim.timeToStart_);
                    gotoNextWord(&buf, 1);

                    int numPlayers;
                    sscanf(buf, "%d", &numPlayers);
                    gotoNextWord(&buf, 1);

                    sim.players_.resize(numPlayers);
                    assert(numPlayers == sim.players_.size());

                    for(int i = 0; i < numPlayers; ++i)
                    {
                        Player& p = sim.players_[i];

                        sscanf(buf, "%f %f %f %d %f %d %d %s %f %d",
                            &p.pos.x, &p.pos.y, &p.vel, &p.dir, &p.dropCooldown, &p.hp, &p.score,
                            p.name, &p.dmgTimer, &p.prevDir);

                        gotoNextWord(&buf, 10);

                    }

                    sim.bombs_.clear();
                    int numBombs;
                    sscanf(buf, "%d", &numBombs);
                    gotoNextWord(&buf, 1);

                    for(int i = 0; i < numBombs; ++i)
                    {
                        Bomb b;

                        sscanf(buf, "%d %d %d %f %d %d ",
                                &b.tile.x, &b.tile.y, &b.range, &b.timer, &b.playerIdxs[0],
                                &b.playerIdxs[1]);

                        sim.bombs_.pushBack(b);

                        gotoNextWord(&buf, 6);
                    }

                    int numExploEvents;
                    sscanf(buf, "%d", &numExploEvents);
                    gotoNextWord(&buf, 1);

                    for(int i = 0; i < numExploEvents; ++i)
                    {
                        ExploEvent e;
                        sscanf(buf, "%d %d %d", &e.tile.x, &e.tile.y, &e.type);
                        exploEvents.pushBack(e);
                        gotoNextWord(&buf, 3);
                    }

                    break;
                }
                case Cmd::InitTileData:
                {
                    newGame = true;

                    // @ !!! we are not validating the data

                    const char* ptr = begin;

                    for(int i = 0; i < Simulation::MapSize * Simulation::MapSize; ++i)
                    {
                        sim.tiles_[0][i] = *ptr - 48; // converting from ascii
                        ptr += 2;
                    }
                }
            }
        }

        const int numToFree = end - recvBuf.data();
        memmove(recvBuf.data(), recvBuf.data() + numToFree, recvBufNumUsed - numToFree);
        recvBufNumUsed -= numToFree;
    }

    // send
    if(!hasToReconnect && sendBuf.size())
    {
        const int rc = send(sockfd, sendBuf.data(), sendBuf.size(), 0);

        if(rc == -1)
        {
            log(logBuf, "send() failed: %s", strerror(errno));
            hasToReconnect = true;
        }
        else
            sendBuf.erase(0, rc);
    }

    // update the tiles based on the exploEvents
    // not inside Cmd::Simulation: because it is inside a loop
    // we want to iterate over all exploEvents just once

    if(!newGame) // so we don't override new map (server called sim.setNewGame())
    {
        for(ExploEvent& e: exploEvents)
        {
            if(e.type == ExploEvent::Crate)
                sim.tiles_[e.tile.y][e.tile.x] = 0;
        }
    }
}

} // netcode

// copuled to the explosion texture asset
Anim createExplosionAnim()
{
    Anim anim;
    anim.frameDt = 0.08f;
    anim.numFrames = 12;

    for(int i = 0; i < anim.numFrames; ++i)
        anim.frames[i] = {i * 96.f, 0.f, 96.f, 96.f};

    return anim;
}

void playSound(FMOD_SOUND* const sound, float volume)
{
    FMOD_CHANNEL* channel;
    FCHECK( FMOD_System_PlaySound(fmodSystem, sound, nullptr, false, &channel) );
    FCHECK( FMOD_Channel_SetVolume(channel, volume) );
}

void Emitter::reserve()
{
    const int maxParticles = particleRanges.life.max * spawn.hz;
    particles.reserve(maxParticles);
    rects.reserve(maxParticles);
}

void Emitter::update(const float dt)
{
    for(int i = 0; i < numActive; ++i)
    {
        rects[i].pos += particles[i].vel * dt;
        particles[i].life -= dt;

        if(particles[i].life <= 0.f)
        {
            particles[i] = particles[numActive - 1];
            rects[i] = rects[numActive - 1];
            --numActive;
            --i;
        }
    }

    spawn.activeTime -= dt;
    if(spawn.activeTime <= 0.f)
        return;

    accumulator += dt;
    const float spawnTime = 1.f / spawn.hz;
    while(accumulator >= spawnTime)
    {
        accumulator -= spawnTime;

        if(numActive + 1 > particles.size())
        {
            particles.pushBack({});
            rects.pushBack({});
        }

        Particle& p = particles[numActive];
        p.life = getRandomFloat(particleRanges.life.min, particleRanges.life.max);
        p.vel.x = getRandomFloat(particleRanges.vel.min.x, particleRanges.vel.max.x);
        p.vel.y = getRandomFloat(particleRanges.vel.min.y, particleRanges.vel.max.y);

        Rect& rect = rects[numActive];

        rect.size = vec2(getRandomFloat(particleRanges.size.min, particleRanges.size.max));

        rect.color.x = getRandomFloat(particleRanges.color.min.x, particleRanges.color.max.x);
        rect.color.y = getRandomFloat(particleRanges.color.min.y, particleRanges.color.max.y);
        rect.color.z = getRandomFloat(particleRanges.color.min.z, particleRanges.color.max.z);
        rect.color.w = getRandomFloat(particleRanges.color.min.w, particleRanges.color.max.w);

        rect.pos = vec2(getRandomFloat(0.f, 1.f), getRandomFloat(0.f, 1.f)) * spawn.size
                   + spawn.pos - rect.size / 2.f;

        ++numActive;
    }
}

void Anim::update(const float dt)
{
    accumulator += dt;
    if(accumulator >= frameDt)
    {
        accumulator -= frameDt;
        ++idx;
        if(idx > numFrames - 1)
        {
            idx = 0;
            ended = true;
        }
    }
}

GameScene::GameScene()
{
    // @TODO: configuration file? :D
    {
        FILE* file;
        file = fopen(".name", "r");

        if(file)
        {
            fgets(nameToSetBuf_, sizeof(nameToSetBuf_), file);
            assert(!fclose(file));
        }
    }

    {
        FILE* file;
        file = fopen(".host", "r");

        if(file)
        {
            fgets(netClient_.host, sizeof(netClient_.host), file);
            assert(!fclose(file));
        }
    }

    glBuffers_ = createGLBuffers();

    font_ = createFontFromFile("res/Exo2-Black.otf", 38, 512);

    textures_.tile = createTextureFromFile("res/tiles.png");

    textures_.player1 = createTextureFromFile("res/player1.png");
    textures_.player2 = createTextureFromFile("res/player2.png");
    textures_.player3 = createTextureFromFile("res/player3.png");
    textures_.player4 = createTextureFromFile("res/player4.png");

    textures_.bomb = createTextureFromFile("res/bomb000.png");
    textures_.explosion = createTextureFromFile("res/Explosion.png");

    FCHECK( FMOD_System_CreateSound(fmodSystem, "res/sfx_exp_various6.wav",
                                    FMOD_CREATESAMPLE, nullptr, &sounds_.bomb) );

    FCHECK( FMOD_System_CreateSound(fmodSystem, "res/sfx_exp_short_hard15.wav",
                                    FMOD_CREATESAMPLE, nullptr, &sounds_.crateExplosion) );

    emitter_.spawn.size = vec2(5.f);
    emitter_.spawn.pos = vec2(210.f);
    assert(emitter_.spawn.pos.x <= (Simulation::MapSize - 1) * Simulation::tileSize_);
    emitter_.spawn.hz = 100.f;
    emitter_.particleRanges.life = {3.f, 6.f};
    emitter_.particleRanges.size = {0.25f, 2.f};
    emitter_.particleRanges.vel = {{-3.5f, -30.f}, {3.5f, -2.f}};
    emitter_.particleRanges.color = {{0.1f, 0.f, 0.f, 0.f}, {0.5f, 0.25f, 0.f, 0.f}};
    emitter_.reserve();


    assert(getSize(playerViews_) == 4 && MaxPlayers == getSize(playerViews_));

    playerViews_[0].texture = &textures_.player1;
    playerViews_[1].texture = &textures_.player2;
    playerViews_[2].texture = &textures_.player3;
    playerViews_[3].texture = &textures_.player4;

    // tightly coupled to the texture assets
    for(int i = Dir::Up; i < Dir::Count; ++i)
    {
        Anim anim;
        anim.frameDt = 0.06f;
        anim.numFrames = 4;
        const float frameSize = 48.f;

        int x;
        switch(i)
        {
            case Dir::Up:    x = 2; break;
            case Dir::Down:  x = 0; break;
            case Dir::Left:  x = 1; break;
            case Dir::Right: x = 3; break;
        }

        for(int i = 0; i < anim.numFrames; ++i)
        {
            anim.frames[i] = {frameSize * x, frameSize * i, frameSize, frameSize};
        }

        for(int j = 0; j < getSize(playerViews_); ++j)
        {
            playerViews_[j].anims[i] = anim;
        }
    }

    // set players in offline simulation

    assert(getSize(inputs_) == 4 && MaxPlayers == getSize(inputs_));

    inputs_[0] = InputType::Player1;
    inputs_[1] = InputType::Player2;
    inputs_[2] = InputType::Bot;
    inputs_[3] = InputType::Bot;

    const int numActive = numActiveInputs();

    offlineSim_.players_.resize(numActive);

    for(int i = 0; i < numActive; ++i)
        sprintf(offlineSim_.players_[i].name, "player%d", i);

    offlineSim_.setNewGame();
}

GameScene::~GameScene()
{
    deleteGLBuffers(glBuffers_);
    deleteFont(font_);
    deleteTexture(textures_.tile);
    deleteTexture(textures_.player1);
    deleteTexture(textures_.player2);
    deleteTexture(textures_.player3);
    deleteTexture(textures_.player4);
    deleteTexture(textures_.bomb);
    deleteTexture(textures_.explosion);
    FCHECK( FMOD_Sound_Release(sounds_.bomb) );
    FCHECK( FMOD_Sound_Release(sounds_.crateExplosion) );
}

void GameScene::processInput(const Array<WinEvent>& events)
{
    for(Action& action: actions_)
        action.drop = false;

    const bool gameStarted =  netClient_.inGame ? (netClient_.sim.timeToStart_ <= 0.f) :
        (offlineSim_.timeToStart_ <= 0.f);

    if(!gameStarted)
        showScore_ = false;

    for(const WinEvent& e: events)
    {
        if(e.type == WinEvent::Key && e.key.action != GLFW_REPEAT)
        {
            const bool on = e.key.action == GLFW_PRESS;

            switch(e.key.key)
            {
                case GLFW_KEY_W:     actions_[0].up    = on; break;
                case GLFW_KEY_S:     actions_[0].down  = on; break;
                case GLFW_KEY_A:     actions_[0].left  = on; break;
                case GLFW_KEY_D:     actions_[0].right = on; break;
                case GLFW_KEY_C:     actions_[0].drop  = on; break;

                case GLFW_KEY_UP:    actions_[1].up    = on; break;
                case GLFW_KEY_DOWN:  actions_[1].down  = on; break;
                case GLFW_KEY_LEFT:  actions_[1].left  = on; break;
                case GLFW_KEY_RIGHT: actions_[1].right = on; break;
                case GLFW_KEY_SPACE: actions_[1].drop  = on; break;

                case GLFW_KEY_ESCAPE:
                {
                    if(on && gameStarted)
                        showScore_ = !showScore_;

                    break;
                }
            }
        }
    }

    // @TODO: do the simulation on the client even if playing online (interpolation)
    if(netClient_.inGame)
        return;

    assert(getSize(actions_) >= 2);

    int idx = 0;
    for(int input: inputs_)
    {
        if(input == InputType::Player1)
            offlineSim_.processPlayerInput(actions_[0], offlineSim_.players_[idx].name);

        else if(input == InputType::Player2)
            offlineSim_.processPlayerInput(actions_[1], offlineSim_.players_[idx].name);

        else if(input == InputType::Bot)
            offlineSim_.updateAndProcessBotInput(offlineSim_.players_[idx].name, frame_.time);
        else
            continue;

        ++idx;
    }
}

// @TODO: client-side prediction
// we need to create another Simulation: predictionSim_
// don't push explo events in predictionSim_.update()

void GameScene::update()
{
    exploEvents_.clear();

    netClient_.update(frame_.time, nameToSetBuf_, exploEvents_, actions_[0]);

    if(!netClient_.inGame)
        offlineSim_.update(frame_.time, exploEvents_);

    emitter_.update(frame_.time);

    {
        const Simulation& sim = netClient_.inGame ? netClient_.sim : offlineSim_;

        for(int i = 0; i < sim.players_.size(); ++i)
        {
            playerViews_[i].anims[sim.players_[i].dir].update(frame_.time);
        }
    }

    for(int i = 0; i < explosions_.size(); ++i)
    {
        Explosion& explo = explosions_[i];
        explo.anim.update(frame_.time);

        if(explo.anim.ended)
        {
            explo = explosions_.back();
            explosions_.popBack();
            --i;
        }
    }

    for(ExploEvent& event: exploEvents_)
    {
        playSound(sounds_.bomb, 0.2f);

        if(event.type == ExploEvent::Wall)
            continue;

        Explosion e;
        e.anim = createExplosionAnim();
        e.tile = event.tile;
        e.size = Simulation::tileSize_ * 2.f;

        if(event.type == ExploEvent::Crate)
            playSound(sounds_.crateExplosion, 0.2f);

        else if(event.type == ExploEvent::Player)
            e.color = {1.f, 0.5f, 0.5f, 0.6f};

        else if(event.type == ExploEvent::OtherBomb)
            e.color = {0.1f, 0.1f, 0.1f, 0.8f};

        else
        {
            e.size = Simulation::tileSize_ * 4.f;
            e.anim.frameDt *= 1.5f;
            e.color = {0.1f, 0.1f, 0.1f, 0.2f};
        }
        
        explosions_.pushBack(e);
    }
}

// this should be static global function
void GameScene::render(const GLuint program)
{
    const Simulation& sim = netClient_.inGame ? netClient_.sim : offlineSim_; // ... there is
    // to much implicit state
    bindProgram(program);

    Camera camera;
    camera.pos = vec2(0.f);
    camera.size = vec2(Simulation::MapSize * sim.tileSize_);
    camera = expandToMatchAspectRatio(camera, frame_.fbSize);
    uniform2f(program, "cameraPos", camera.pos);
    uniform2f(program, "cameraSize", camera.size);

    // tilemap

    assert(Simulation::MapSize * Simulation::MapSize <= getSize(rects_));

    for (int j = 0; j < Simulation::MapSize; ++j)
    {
        for (int i = 0; i < Simulation::MapSize; ++i)
        {
            Rect& rect = rects_[j * Simulation::MapSize + i];
            rect.pos = vec2(i, j) * sim.tileSize_;
            rect.size = vec2(sim.tileSize_);
            rect.color = {1.f, 1.f, 1.f, 1.f};

            switch (sim.tiles_[j][i])
            {
                case 0: rect.texRect = {0.f, 0.f, 64.f, 64.f};   break;
                case 1: rect.texRect = {64.f, 0.f, 64.f, 64.f};  break;
                case 2: rect.texRect = {128.f, 0.f, 32.f, 32.f};
                        rect.color = {0.25f, 0.25f, 0.25f, 1.f}; break;
            }

            rect.texRect.x /= textures_.tile.size.x;
            rect.texRect.y /= textures_.tile.size.y;
            rect.texRect.z /= textures_.tile.size.x;
            rect.texRect.w /= textures_.tile.size.y;
        }
    }

    uniform1i(program, "mode", FragmentMode::Texture);
    bindTexture(textures_.tile);
    updateGLBuffers(glBuffers_, rects_, getSize(rects_));
    renderGLBuffers(glBuffers_, getSize(rects_));

    // bombs

    assert(sim.bombs_.size() <= getSize(rects_));

    for(int i = 0; i < sim.bombs_.size(); ++i)
    {
        // @ somewhat specific to the bomb texture asset
        const float coeff = fabs(sinf(sim.bombs_[i].timer * 2.f)) * 0.4f;

        Rect& rect = rects_[i];
        rect.size = vec2(sim.tileSize_ + coeff * sim.tileSize_);
        rect.pos = vec2(sim.bombs_[i].tile) * sim.tileSize_ + (vec2(sim.tileSize_)
                - rect.size) / 2.f;

        // default values might be overwritten
        rect.color = {1.f, 1.f, 1.f, 1.f};
        rect.texRect = {0.f, 0.f, 1.f, 1.f};
    }

    uniform1i(program, "mode", FragmentMode::Texture);
    bindTexture(textures_.bomb);
    updateGLBuffers(glBuffers_, rects_, sim.bombs_.size());
    renderGLBuffers(glBuffers_, sim.bombs_.size());

    // players

    for(int i = 0; i < sim.players_.size(); ++i)
    {
        const Player& player = sim.players_[i];

        if(player.hp == 0)
            continue;

        PlayerView& playerView = playerViews_[i];

        Rect rect;
        rect.size = vec2(sim.tileSize_);
        rect.pos = player.pos;

        if(player.dmgTimer > 0.f)
            rect.color = {1.f, 0.2f, 0.2f, 0.3f};
        else
            rect.color = {1.f, 1.f, 1.f, 0.15f};

        uniform1i(program, "mode", FragmentMode::Color);
        updateGLBuffers(glBuffers_, &rect, 1);
        renderGLBuffers(glBuffers_, 1);

        rect.color = {1.f, 1.f, 1.f, 1.f};

        rect.texRect = player.dir ? playerView.anims[player.dir].getCurrentFrame() :
                                    playerView.anims[player.prevDir].frames[0];

        rect.texRect.x /= playerView.texture->size.x;
        rect.texRect.y /= playerView.texture->size.y;
        rect.texRect.z /= playerView.texture->size.x;
        rect.texRect.w /= playerView.texture->size.y;

        uniform1i(program, "mode", FragmentMode::Texture);
        bindTexture(*playerView.texture);
        updateGLBuffers(glBuffers_, &rect, 1);
        renderGLBuffers(glBuffers_, 1);
    }

    // explosions

    assert(explosions_.size() <= getSize(rects_));

    for(int i = 0; i < explosions_.size(); ++i)
    {
        rects_[i].size = vec2(explosions_[i].size);

        rects_[i].pos = vec2(explosions_[i].tile) * sim.tileSize_
                        + ( vec2(sim.tileSize_) - rects_[i].size ) / 2.f;

        rects_[i].color = explosions_[i].color;
        rects_[i].texRect = explosions_[i].anim.getCurrentFrame();
        rects_[i].texRect.x /= textures_.explosion.size.x;
        rects_[i].texRect.y /= textures_.explosion.size.y;
        rects_[i].texRect.z /= textures_.explosion.size.x;
        rects_[i].texRect.w /= textures_.explosion.size.y;
    }

    uniform1i(program, "mode", FragmentMode::Texture);
    bindTexture(textures_.explosion);
    updateGLBuffers(glBuffers_, rects_, explosions_.size());
    renderGLBuffers(glBuffers_, explosions_.size());

    // particles

    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    uniform1i(program, "mode", FragmentMode::Color);
    updateGLBuffers(glBuffers_, emitter_.rects.data(), emitter_.numActive);
    renderGLBuffers(glBuffers_, emitter_.numActive);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // bars

    {
        Rect* rect = &rects_[0];
        const float h = 2.f;
        for(const Player& player: sim.players_)
        {
            if(player.hp == 0)
                continue;

            // * hp
            rect[0].pos = player.pos;
            rect[0].size = {float(player.hp) /Simulation::HP * sim.tileSize_, h};
            rect[0].color = {1.f, 0.15f, 0.15f, 0.7f};

            // * drop cooldown
            rect[1].pos = rect[0].pos;
            rect[1].pos.y += h;
            rect[1].size = {player.dropCooldown / sim.dropCooldown_ * sim.tileSize_, h};
            rect[1].color = {1.f, 1.f, 0.f, 0.6f};

            rect += 2;
        }

        uniform1i(program, "mode", FragmentMode::Color);
        updateGLBuffers(glBuffers_, rects_, rect - rects_);
        renderGLBuffers(glBuffers_, rect - rects_);
    }

    // names
    if(netClient_.inGame)
    {
        uniform1i(program, "mode", FragmentMode::Font);
        bindTexture(font_.texture);

        int textCount = 0;

        Text text;
        text.color = {0.1f, 1.f, 0.1f, 0.85f};
        text.scale = 0.15f;

        for(const Player& player: sim.players_)
        {
            if(player.hp == 0)
                continue;

            text.str = player.name;
            text.pos = player.pos;
            text.pos.y -= 6.f;

            textCount += writeTextToBuffer(text, font_, rects_ + textCount, getSize(rects_));
        }

        updateGLBuffers(glBuffers_, rects_, textCount);
        renderGLBuffers(glBuffers_, textCount);
    }


    // new round timer

    if(sim.timeToStart_ > 0.f)
    {
        char buffer[20];
        Text text;
        text.str = buffer;
        snprintf(buffer, getSize(buffer), "%.3f", sim.timeToStart_);
        text.color = {1.f, 0.5f, 1.f, 0.8f};
        text.scale = 2.f;
        text.pos = {(Simulation::MapSize * sim.tileSize_ - getTextSize(text, font_).x)
                    / 2.f, 5.f};

        const int count = writeTextToBuffer(text, font_, rects_, getSize(rects_));

        uniform1i(program, "mode", FragmentMode::Font);
        bindTexture(font_.texture);
        updateGLBuffers(glBuffers_, rects_, count);
        renderGLBuffers(glBuffers_, count);
    }

    // score

    if(sim.timeToStart_ > 0.f || showScore_)
    {
        char buffer[256];
        Text text;
        text.color = {1.f, 1.f, 0.f, 0.8f};
        text.scale = 0.8f;
        text.str = buffer;

        int bufOffset = 0;
        bufOffset += snprintf(buffer + bufOffset, max(0, getSize(buffer) - bufOffset),
                              "score:");

        for(const Player& player: sim.players_)
        {
            bufOffset += snprintf(buffer + bufOffset, max(0, getSize(buffer) - bufOffset),
                                  "\n%d", player.score);
        }

        const vec2 textSize = getTextSize(text, font_);
        text.pos = ( vec2(Simulation::MapSize) * sim.tileSize_ - textSize ) / 2.f;

        // * background
        {
            const float border = 5.f;
            Rect rect;
            rect.pos = vec2(text.pos - border);
            rect.size = textSize + 2.f * border;
            rect.color = {0.f, 0.f, 0.f, 0.85f};
            uniform1i(program, "mode", FragmentMode::Color);
            updateGLBuffers(glBuffers_, &rect, 1);
            renderGLBuffers(glBuffers_, 1);
        }

        // * text
        {
            const int textCount = writeTextToBuffer(text, font_, rects_, getSize(rects_));
            uniform1i(program, "mode", FragmentMode::Font);
            bindTexture(font_.texture);
            updateGLBuffers(glBuffers_, rects_, textCount);
            renderGLBuffers(glBuffers_, textCount);
        }

        // * avatars

        uniform1i(program, "mode", FragmentMode::Texture);

        const float lineSpace = text.scale * font_.lineSpace;
        Rect rect;
        rect.size = vec2(20.f);
        rect.pos = {text.pos.x + textSize.x - rect.size.x, text.pos.y};

        for(int i = 0; i < sim.players_.size(); ++i)
        {
            const Player& player = sim.players_[i];
            const PlayerView& playerView = playerViews_[i];
            rect.pos.y += lineSpace;
            rect.texRect = player.dir ? playerView.anims[player.dir].getCurrentFrame() :
                                        playerView.anims[player.prevDir].frames[0];

            rect.texRect.x /= playerView.texture->size.x;
            rect.texRect.y /= playerView.texture->size.y;
            rect.texRect.z /= playerView.texture->size.x;
            rect.texRect.w /= playerView.texture->size.y;

            bindTexture(*playerView.texture);
            updateGLBuffers(glBuffers_, &rect, 1);
            renderGLBuffers(glBuffers_, 1);
        }
    }

    // imgui

    ImGui::Begin("cavetiles");
    ImGui::Spacing();

    ImGui::Text("offline mode inputs:");
    {
        const char* itypes[] =
        {
            "none",
            "player 1",
            "player 2",
            "bot"
        };

        const int prevNumActiveInputs = numActiveInputs();

        for(int i = 0; i < getSize(inputs_); ++i)
        {
            ImGui::Text("%d.", i);
            ImGui::SameLine();
            ImGui::PushID(i);
            ImGui::Combo("", &inputs_[i], itypes, getSize(itypes));
            ImGui::PopID();
        }
        
        const int newNumActiveInputs = numActiveInputs();

        if(prevNumActiveInputs != newNumActiveInputs)
        {
            offlineSim_.players_.resize(newNumActiveInputs);
            offlineSim_.setNewGame();
        }
    }

    ImGui::Text("controls:\n"
                "\n"
                "   Esc    - display score\n"
                "\n"
                "player1:\n"
                "   WSAD   - move\n"
                "   C      - drop a bomb\n"
                "\n"
                "player2:\n"
                "   ARROWS - move\n"
                "   SPACE  - drop a bomb\n");

    ImGui::Spacing();

    if(netClient_.inGame)
        ImGui::TextColored(ImVec4(0.f, 1.f, 0.f, 1.f), "logged as '%s'", netClient_.inGameName);

    else if(!netClient_.hasToReconnect)
        ImGui::TextColored(ImVec4(1.f, 1.f, 0.f, 1.f), "status: connected, waiting in the "
                "lobby...");

    else
        ImGui::TextColored(ImVec4(1.f, 0.3f, 0.f, 1.f), "status: connecting to '%s'",
                netClient_.host);

    if(ImGui::InputText("host name", hostnameBuf_, sizeof(hostnameBuf_),
                ImGuiInputTextFlags_EnterReturnsTrue))
    {
        assert(sizeof(hostnameBuf_) == sizeof(netClient_.host));

        if(strcmp(hostnameBuf_, netClient_.host) != 0)
        {
            netClient_.hasToReconnect = true;
            memcpy(netClient_.host, hostnameBuf_, sizeof(netClient_.host));

            FILE* file;
            file = fopen(".host", "w");
            assert(file);
            fputs(hostnameBuf_, file);
            assert(!fclose(file));
        }
    }

    if(ImGui::InputText("login name", inputNameBuf_, sizeof(inputNameBuf_),
                ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CharsNoBlank))
        // no blank because the serialization system on the server requires it
    {
        assert(sizeof(nameToSetBuf_) == sizeof(inputNameBuf_));

        memcpy(nameToSetBuf_, inputNameBuf_, sizeof(nameToSetBuf_));

        FILE* file;
        file = fopen(".name", "w");
        assert(file);
        fputs(nameToSetBuf_, file);
        assert(!fclose(file));

        netcode::addMsg(netClient_.sendBuf, netcode::Cmd::SetName, nameToSetBuf_);
    }

    ImGui::Spacing();
    ImGui::Text("netcode::Client log");
    ImGui::InputTextMultiline("##netcode::NetClient log", netClient_.logBuf.data(),
        netClient_.logBuf.size(), ImVec2(300.f, 0.f), ImGuiInputTextFlags_ReadOnly);

    ImGui::Spacing();
    ImGui::Text("send chat msg");

    if(ImGui::InputText("##chatbuf", chatBuf_, sizeof(chatBuf_),
                ImGuiInputTextFlags_EnterReturnsTrue))
    {
        addMsg(netClient_.sendBuf, netcode::Cmd::Chat, chatBuf_);
        chatBuf_[0] = '\0';
    }

    ImGui::End();
}
