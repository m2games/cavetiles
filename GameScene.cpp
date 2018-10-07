#include "Scene.hpp"
#include "imgui/imgui.h"
#include "GLFW/glfw3.h"
#include <math.h>
#include <stdio.h>

// includes for the netcode
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <netinet/tcp.h>

SIM_STATIC_DEF

namespace netcode
{

const char* getCmdStr(int cmd)
{
    switch(cmd)
    {
        case Cmd::Ping: return "PING";
        case Cmd::Pong: return "PONG";
        case Cmd::Name: return "NAME";
        case Cmd::Chat: return "CHAT";
    }
    assert(false);
}

void addMsg(Array<char>& sendBuf, int cmd, const char* payload)
{
    const char* cmdStr = getCmdStr(cmd);
    int len = strlen(cmdStr) + strlen(payload) + 2; // ' ' + '\0'
    int prevSize = sendBuf.size();
    sendBuf.resize(prevSize + len);
    assert(snprintf(sendBuf.data() + prevSize, len, "%s %s", cmdStr, payload) == len - 1);
}

const void* get_in_addr(const sockaddr* const sa)
{
    if(sa->sa_family == AF_INET)
        return &( ( (sockaddr_in*)sa )->sin_addr );

    return &( ( (sockaddr_in6*)sa )->sin6_addr );
}

void addLogMsg(Array<char>& buf, const char* const msg)
{
    if(buf.size() > 1000)
        buf.clear();

    int len = strlen(msg) + 1; // '\0'

    int prevSize = buf.size();

    if(prevSize)
        prevSize -= 1; // so we will overwrite the null character

    buf.resize(prevSize + len);
    assert(snprintf(buf.data() + prevSize, len, "%s", msg) == len - 1);
}

void addLogMsgErno(Array<char>& buf, const char* const msg, bool gaiEc = 0)
{
    // this sucks, sucks, sucks...

    addLogMsg(buf, msg);
    addLogMsg(buf, ": ");

    if(!gaiEc)
        addLogMsg(buf, strerror(errno));
    else
        addLogMsg(buf, gai_strerror(gaiEc));

    addLogMsg(buf, "\n");
}

// returns socket descriptior, -1 if failed
// if succeeded you have to free the socket yourself
// @TODO: this must be non-blocking connect
int connect(Array<char>& logBuf)
{
    addrinfo hints = {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* list;
    {
        // this block (domain name resolution)
        const int ec = getaddrinfo("localhost", "3000", &hints, &list);
        if(ec != 0)
        {
            addLogMsgErno(logBuf, "getaddrinfo() failed", ec);
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
            addLogMsgErno(logBuf, "socket() failed");
            
            continue;
        }

        if(connect(sockfd, it->ai_addr, it->ai_addrlen) == -1)
        {
            close(sockfd);

            addLogMsgErno(logBuf, "connect() failed");

            continue;
        }

        char name[INET6_ADDRSTRLEN];
        inet_ntop(it->ai_family, get_in_addr(it->ai_addr), name, sizeof(name));

        // fuck...
        addLogMsg(logBuf, "connected to ");
        addLogMsg(logBuf, name);
        addLogMsg(logBuf, "\n");
        break;
    }
    freeaddrinfo(list);

    if(it == nullptr)
    {
        addLogMsg(logBuf, "connection procedure failed\n");
        return -1;
    }

    if(fcntl(sockfd, F_SETFL, O_NONBLOCK) == -1)
    {
        close(sockfd);

        addLogMsgErno(logBuf, "fcntl() failed");

        return -1;
    }

    {
        const int option = 1;
        if(setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &option, sizeof(option)) == -1)
        {
            close(sockfd);

            addLogMsgErno(logBuf, "setsockopt() (TCP_NODELAY) failed");

            return -1;
        }
    }

    return sockfd;
}

Client::Client()
{
    sendBuf.reserve(500);
    recvBuf.resize(500);
    logBuf.reserve(500);
    addLogMsg(logBuf, ""); // to terminate with null
}

Client::~Client()
{
    if(sockfd != -1)
        close(sockfd);
}

void Client::update(const float dt, const char* name, FixedArray<ExploEvent, 50>& eevents,
                    Action& playerAction)
{
    // @TODO: push new events to eevents; send playerAction to server; update simulation

    // time managment
    timerAlive += dt;
    timerReconnect += dt;

    if(hasToReconnect)
    {
        if(timerReconnect >= timerReconnectMax)
        {
            timerReconnect = 0.f;

            if(sockfd != -1)
                close(sockfd);

            sockfd = connect(logBuf);

            if(sockfd != -1)
            {
                serverAlive = true;
                timerAlive = timerAliveMax;
                hasToReconnect = false;
                sendBuf.clear();

                // send the player name
                int len = strlen(name);
                assert(len < maxNameSize);
                assert(len);
                
                addMsg(sendBuf, Cmd::Name, name);
            }
        }
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
                addLogMsg(logBuf, "no PONG response from server, will try to reconnect\n");
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
                    addLogMsgErno(logBuf, "recv() failed");

                    hasToReconnect = true;
                }
                break;
            }
            else if(rc == 0)
            {
                addLogMsg(logBuf, "server has closed the connection\n");
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
                    addLogMsg(logBuf, "recvBuf big size issue, exiting\n");
                    assert(false);
                    break;
                }
            }
        }
    }

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
                    // fuck...

                    addLogMsg(logBuf, "WARNING unknown command received: ");
                    addLogMsg(logBuf, begin);
                    addLogMsg(logBuf, "\n");

                    break;

                case Cmd::Ping:
                    addMsg(sendBuf, Cmd::Pong);
                    break;

                case Cmd::Pong:
                    serverAlive = true;
                    break;

                case Cmd::Name:
                    hasToReconnect = true;
                    addLogMsg(logBuf, "name already in use, try something different\n");
                    break;

                case Cmd::Chat:
                    addLogMsg(logBuf, begin);
                    addLogMsg(logBuf, "\n");
                    break;
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
            addLogMsgErno(logBuf, "send() failed");

            hasToReconnect = true;
        }
        else
            sendBuf.erase(0, rc);
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

void Bomb::addPlayer(const int idx)
{
    for(int& i: playerIdxs)
    {
        assert(i != idx);
        if(i == -1)
        {
            i = idx;
            return;
        }
    }
    assert(false);
}

void Bomb::removePlayer(const int idx)
{
    for(int& i: playerIdxs)
    {
        if(i == idx)
        {
            i = -1;
            return;
        }
    }
    assert(false);
}

bool Bomb::findPlayer(const int idx) const
{
    for(const int i: playerIdxs)
    {
        if(i == idx)
            return true;
    }
    return false;
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

ivec2 getPlayerTile(const Player& player, const float tileSize)
{
    return ivec2(player.pos / tileSize + 0.5f);
}

bool isCollision(const vec2 playerPos, const ivec2 tile, const float tileSize)
{
    const vec2 tilePos = vec2(tile) * tileSize;

    return playerPos.x < tilePos.x + tileSize &&
           playerPos.x + tileSize > tilePos.x &&
           playerPos.y < tilePos.y + tileSize &&
           playerPos.y + tileSize > tilePos.y;
}

float length(const vec2 v)
{
    return sqrt(v.x * v.x + v.y * v.y);
}

vec2 normalize(const vec2 v)
{
    return v / length(v);
}

float dot(const vec2 v1, const vec2 v2)
{
    return v1.x * v2.x + v1.y * v2.y;
}

Simulation::Simulation()
{
    assert(MapSize % 2);

    {
        Bomb bomb;
        assert(getSize(bomb.playerIdxs) == getSize(players_));
    }

    for(int i = 0; i < getSize(players_); ++i)
    {
        Player& player = players_[i];

        sprintf(player.name, "player%d", i);

        player.vel = 80.f;
    }

    // tilemap
    // * edges

    for(int i = 0; i < MapSize; ++i)
    {
        tiles_[0][i] = 2;
        tiles_[MapSize - 1][i] = 2;
        tiles_[i][0] = 2;
        tiles_[i][MapSize - 1] = 2;
    }

    // * pillars

    for(int i = 2; i < MapSize - 1; i += 2)
    {
        for(int j = 2; j < MapSize - 1; j += 2)
        {
            tiles_[j][i] = 2;
        }
    }

    setNewGame();
}

void Simulation::setNewGame()
{
    bombs_.clear();

    for(Player& player: players_)
    {
        player.dropCooldown = 0.f;
        player.hp = HP;
        // so player.prevDir (***) won't be overwritten in processPlayerInput()
        player.dir = Dir::Nil;
    }

    // specific configuration for each player
    assert(getSize(players_) == 2);

    players_[0].pos = vec2(tileSize_ * 1);
    players_[0].prevDir = Dir::Down; // ***

    players_[1].pos = vec2(tileSize_ * (MapSize - 2));
    players_[1].prevDir = Dir::Left;

    // tilemap
    // * crates

    int freeTiles[MapSize * MapSize];
    int numFreeTiles = 0;

    // delete crates from previous game
    for(int i = 0; i < MapSize * MapSize; ++i)
    {
        if(tiles_[0][i] == 1)
            tiles_[0][i] = 0;
    }

    for(int i = 0; i < MapSize * MapSize; ++i)
    {
        if(tiles_[0][i] == 0)
        {
            // check if it is not adjacent to the players

            const ivec2 targetTile = {i % MapSize, i / MapSize};

            for(const Player& player: players_)
            {
                for(int k = -1; k < 2; ++k)
                {
                    for(int l = -1; l < 2; ++l)
                    {
                        const ivec2 adjacentTile = getPlayerTile(player, tileSize_)
                                                   + ivec2(k, l);

                        if(targetTile == adjacentTile)
                            goto end;
                    }
                }
            }
            freeTiles[numFreeTiles] = i;
            ++numFreeTiles;
        }
end:;
    }

    const int numCrates = numFreeTiles * 2 / 3;
    for(int i = 0; i < numCrates; ++i)
    {
        const int freeTileIdx = getRandomInt(0, numFreeTiles - 1);
        const int tileIdx = freeTiles[freeTileIdx];
        freeTiles[freeTileIdx] = freeTiles[numFreeTiles - 1];
        tiles_[0][tileIdx] = 1;
        numFreeTiles -= 1;
    }
}

void Simulation::processPlayerInput(const Action& action, const char* name)
{
    if(timeToStart_ > 0.f)
        return;

    Player* pptr = nullptr;

    for(Player& p: players_)
    {
        if(!strncmp(name, p.name, 20))
                pptr = &p;
    }

    assert(pptr);

    Player& player = *pptr;

    if(player.dir)
        player.prevDir = player.dir;

    if     (action.left)  player.dir = Dir::Left;
    else if(action.right) player.dir = Dir::Right;
    else if(action.up)    player.dir = Dir::Up;
    else if(action.down)  player.dir = Dir::Down;
    else                  player.dir = Dir::Nil;

    if(action.drop)
    {
        const ivec2 targetTile = getPlayerTile(player, tileSize_);
        bool freeTile = true;

        for(const Bomb& bomb: bombs_)
        {
            if(bomb.tile == targetTile)
            {
                freeTile = false;
                break;
            }
        }

        if(freeTile && player.dropCooldown == 0.f)
        {
            player.dropCooldown = dropCooldown_;
            Bomb bomb;
            bomb.tile = targetTile;
            bomb.timer = 3.f;

            for(const Player& player: players_)
            {
                if(isCollision(player.pos, targetTile, tileSize_))
                {
                    const int playerIdx = &player - players_;
                    bomb.addPlayer(playerIdx);
                }
            }

            bombs_.pushBack(bomb);
        }
    }
}

void Simulation::update(float dt, FixedArray<ExploEvent, 50>& exploEvents)
{
    timeToStart_ -= dt;

    // @TODO(matiTechno): replace with 'gaffer on games' technique
    dt = min(dt, 0.033f);

    // bombs

    for(int bombIdx = 0; bombIdx < bombs_.size(); ++bombIdx)
    {
        Bomb& bomb = bombs_[bombIdx];
        bomb.timer -= dt;

        if(bomb.timer > 0.f)
            continue;

        for(int dirIdx = Dir::Nil; dirIdx < Dir::Count; ++dirIdx)
        {
            const vec2 dir = dirVecs_[dirIdx];
            const int range = (dirIdx != Dir::Nil) ? bomb.range : 1;

            for(int step = 1; step <= range; ++step)
            {
                const ivec2 tile = bomb.tile + ivec2(dir) * step;
                int& tileValue = tiles_[tile.y][tile.x];

                if(tileValue == 2)
                {
                    exploEvents.pushBack({tile, ExploEvent::Wall});
                    break;
                }

                else if(tileValue == 1)
                {
                    tileValue = 0;
                    exploEvents.pushBack({tile, ExploEvent::Crate});
                    break;
                }
                else
                {
                    bool hitBomb = false;
                    // @ shadowing
                    for(Bomb& bomb: bombs_)
                    {
                        if(bomb.tile == tile)
                        {
                            hitBomb = true;
                            // explode in the near future
                            bomb.timer = min(bomb.timer, 0.1f);
                        }
                    }

                    bool hitPlayer = false;
                    for(Player& player: players_)
                    {
                        if(getPlayerTile(player, tileSize_) == tile && player.hp)
                        {
                            player.hp -= 1;
                            player.dmgTimer = 1.2f;
                            hitPlayer = true;
                        }
                    }

                    ExploEvent ee;
                    ee.tile = tile;

                    if(hitPlayer)
                        ee.type = ExploEvent::Player;
                    else if(hitBomb)
                        ee.type = ExploEvent::OtherBomb;
                    else
                        ee.type = ExploEvent::EmptyTile;

                    exploEvents.pushBack(ee);
                }
            }
        }
        bomb = bombs_.back();
        bombs_.popBack();
        --bombIdx;
    }

    // players

    int playerIdx = -1;

    for(Player& player: players_)
    {
        ++playerIdx;

        player.pos += player.vel * dt * dirVecs_[player.dir];
        player.dropCooldown -= dt;
        player.dropCooldown = max(0.f, player.dropCooldown);
        player.dmgTimer -= dt;

        // collisions
        // @TODO(matiTechno): unify collision code for tiles and bombs?

        const ivec2 playerTile = getPlayerTile(player, tileSize_);

        // * with bombs

        for(Bomb& bomb: bombs_)
        {
            const bool collision = isCollision(player.pos, bomb.tile, tileSize_);
            const bool allowed = bomb.findPlayer(playerIdx);

            if(collision && !allowed)
            {
                if(dirVecs_[player.dir].x)
                    player.pos.x = playerTile.x * tileSize_;
                else
                    player.pos.y = playerTile.y * tileSize_;
            }
            else if(!collision && allowed)
            {
                bomb.removePlayer(playerIdx);
            }
        }

        // * with tiles

        bool collision = false;

        for(int i = -1; i < 2; ++i)
        {
            for(int j = -1; j < 2; ++j)
            {
                const ivec2 tile = playerTile + ivec2(i, j);

                if(tiles_[tile.y][tile.x] != 0 && isCollision(player.pos, tile, tileSize_))
                {
                    collision = true;
                    goto end;
                }
            }
        }
end:
        if(collision)
        {
            if(dirVecs_[player.dir].x)
                player.pos.x = playerTile.x * tileSize_;
            else
                player.pos.y = playerTile.y * tileSize_;

            // sliding on the corner mechanic

            const vec2 offset = player.pos - vec2(playerTile) * tileSize_;
            ivec2 slideTile;

            if( (length(offset) > tileSize_ / 4.f) &&
                (tiles_[int(playerTile.y + dirVecs_[player.dir].y)]
                       [int(playerTile.x + dirVecs_[player.dir].x)] != 0) )
            {
                slideTile = playerTile + ivec2(normalize(offset));
                // @ matiTechno
                assert(slideTile != playerTile);
            }
            else
                slideTile = playerTile;

            const vec2 slideTilePos = vec2(slideTile) * tileSize_;

            // important: y first, x second
            // check if a tile next to slideTile (in the player direction) is free
            if(tiles_[int(slideTile.y + dirVecs_[player.dir].y)]
                     [int(slideTile.x + dirVecs_[player.dir].x)] == 0)
            {
                const vec2 slideVec = slideTilePos - player.pos;
                const vec2 slideDir = normalize(slideVec);

                player.pos += player.vel * dt * slideDir;

                const vec2 newSlideVec = slideTilePos - player.pos;

                if(dot(slideVec, newSlideVec) < 0.f)
                    player.pos = slideTilePos;
            }
        }
    }

    // score; game state

    int numLosers = 0;

    for(const Player& player: players_)
        numLosers += (player.hp == 0);

    if(numLosers >= getSize(players_) - 1)
    {
        for(Player& player: players_)
            player.score += player.hp > 0;

        timeToStart_ = 3.f;
        setNewGame();
    }
}

GameScene::GameScene()
{
    assert(getSize(playerViews_) == getSize(sim_.players_));

    {
        FILE* file;
        file = fopen(".name", "r");

        if(file)
        {
            fgets(nameBuf_, sizeof(nameBuf_), file);
            assert(!fclose(file));
        }
    }

    glBuffers_ = createGLBuffers();

    font_ = createFontFromFile("res/Exo2-Black.otf", 38, 512);

    textures_.tile = createTextureFromFile("res/tiles.png");
    textures_.player1 = createTextureFromFile("res/player1.png");
    textures_.player2 = createTextureFromFile("res/player2.png");
    textures_.bomb = createTextureFromFile("res/bomb000.png");
    textures_.explosion = createTextureFromFile("res/Explosion.png");

    FCHECK( FMOD_System_CreateSound(fmodSystem, "res/sfx_exp_various6.wav",
                                    FMOD_CREATESAMPLE, nullptr, &sounds_.bomb) );

    FCHECK( FMOD_System_CreateSound(fmodSystem, "res/sfx_exp_short_hard15.wav",
                                    FMOD_CREATESAMPLE, nullptr, &sounds_.crateExplosion) );

    emitter_.spawn.size = vec2(5.f);
    emitter_.spawn.pos = vec2(210.f);
    assert(emitter_.spawn.pos.x <= (Simulation::MapSize - 1) * sim_.tileSize_);
    emitter_.spawn.hz = 100.f;
    emitter_.particleRanges.life = {3.f, 6.f};
    emitter_.particleRanges.size = {0.25f, 2.f};
    emitter_.particleRanges.vel = {{-3.5f, -30.f}, {3.5f, -2.f}};
    emitter_.particleRanges.color = {{0.1f, 0.f, 0.f, 0.f}, {0.5f, 0.25f, 0.f, 0.f}};
    emitter_.reserve();


    // specific configuration for each player view
    assert(getSize(playerViews_) == 2);

    playerViews_[0].texture = &textures_.player1;
    playerViews_[1].texture = &textures_.player2;

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

        for(int j = 0; j < 2; ++j)
        {
            playerViews_[j].anims[i] = anim;
        }
    }
}

GameScene::~GameScene()
{
    deleteGLBuffers(glBuffers_);
    deleteFont(font_);
    deleteTexture(textures_.tile);
    deleteTexture(textures_.player1);
    deleteTexture(textures_.player2);
    deleteTexture(textures_.bomb);
    deleteTexture(textures_.explosion);
    FCHECK( FMOD_Sound_Release(sounds_.bomb) );
    FCHECK( FMOD_Sound_Release(sounds_.crateExplosion) );
}

void GameScene::processInput(const Array<WinEvent>& events)
{
    for(Action& action: actions_)
        action.drop = false;

    const bool gameStarted = sim_.timeToStart_ <= 0.f;

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

    if(!netClient_.hasToReconnect)
        return;

    assert(getSize(sim_.players_) >= getSize(actions_));

    for(int i = 0; i < getSize(actions_); ++i)
    {
        Action& action = actions_[i];
        sim_.processPlayerInput(action, sim_.players_[i].name);
    }
}

void GameScene::update()
{
    exploEvents_.clear();

    netClient_.update(frame_.time, nameBuf_, exploEvents_, actions_[0]);

    // @TODO: do the simulation on the client even if connected to server
    if(netClient_.hasToReconnect)
        sim_.update(frame_.time, exploEvents_);

    // sync the local simulation with the one on the server; for now without interpolating
    else
    {
        // @ enable after implementing simulation protocol; make sure netClient_.sim
        // contains valid data
        // memcpy(&sim_, netClient_.sim, sizeof(Simulation);
    }


    emitter_.update(frame_.time);

    for(int i = 0; i < getSize(sim_.players_); ++i)
    {
        playerViews_[i].anims[sim_.players_[i].dir].update(frame_.time);
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
        e.size = sim_.tileSize_ * 2.f;

        if(event.type == ExploEvent::Crate)
            playSound(sounds_.crateExplosion, 0.2f);

        else if(event.type == ExploEvent::Player)
            e.color = {1.f, 0.5f, 0.5f, 0.6f};

        else if(event.type == ExploEvent::OtherBomb)
            e.color = {0.1f, 0.1f, 0.1f, 0.8f};

        else
        {
            e.size = sim_.tileSize_ * 4.f;
            e.anim.frameDt *= 1.5f;
            e.color = {0.1f, 0.1f, 0.1f, 0.2f};
        }
        
        explosions_.pushBack(e);
    }
}

void GameScene::render(const GLuint program)
{
    bindProgram(program);

    Camera camera;
    camera.pos = vec2(0.f);
    camera.size = vec2(Simulation::MapSize * sim_.tileSize_);
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
            rect.pos = vec2(i, j) * sim_.tileSize_;
            rect.size = vec2(sim_.tileSize_);
            rect.color = {1.f, 1.f, 1.f, 1.f};

            switch (sim_.tiles_[j][i])
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

    assert(sim_.bombs_.size() <= getSize(rects_));

    for(int i = 0; i < sim_.bombs_.size(); ++i)
    {
        // @ somewhat specific to the bomb texture asset
        const float coeff = fabs(sinf(sim_.bombs_[i].timer * 2.f)) * 0.4f;

        Rect& rect = rects_[i];
        rect.size = vec2(sim_.tileSize_ + coeff * sim_.tileSize_);
        rect.pos = vec2(sim_.bombs_[i].tile) * sim_.tileSize_ + (vec2(sim_.tileSize_)
                - rect.size) / 2.f;

        // default values might be overwritten
        rect.color = {1.f, 1.f, 1.f, 1.f};
        rect.texRect = {0.f, 0.f, 1.f, 1.f};
    }

    uniform1i(program, "mode", FragmentMode::Texture);
    bindTexture(textures_.bomb);
    updateGLBuffers(glBuffers_, rects_, sim_.bombs_.size());
    renderGLBuffers(glBuffers_, sim_.bombs_.size());

    // players

    for(int i = 0; i < getSize(sim_.players_); ++i)
    {
        Player& player = sim_.players_[i];
        PlayerView& playerView = playerViews_[i];

        Rect rect;
        rect.size = vec2(sim_.tileSize_);
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

        rects_[i].pos = vec2(explosions_[i].tile) * sim_.tileSize_
                        + ( vec2(sim_.tileSize_) - rects_[i].size ) / 2.f;

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
        for(const Player& player: sim_.players_)
        {
            // * hp
            rect[0].pos = player.pos;
            rect[0].size = {float(player.hp) /Simulation::HP * sim_.tileSize_, h};
            rect[0].color = {1.f, 0.15f, 0.15f, 0.7f};

            // * drop cooldown
            rect[1].pos = rect[0].pos;
            rect[1].pos.y += h;
            rect[1].size = {player.dropCooldown / sim_.dropCooldown_ * sim_.tileSize_, h};
            rect[1].color = {1.f, 1.f, 0.f, 0.6f};

            rect += 2;
        }

        uniform1i(program, "mode", FragmentMode::Color);
        updateGLBuffers(glBuffers_, rects_, getSize(sim_.players_) * 2.f);
        renderGLBuffers(glBuffers_, getSize(sim_.players_) * 2.f);
    }

    // names
    if(!netClient_.hasToReconnect)
    {
        uniform1i(program, "mode", FragmentMode::Font);
        bindTexture(font_.texture);

        int textCount = 0;

        Text text;
        text.color = {0.1f, 1.f, 0.1f, 0.85f};
        text.scale = 0.15f;

        for(Player& player: sim_.players_)
        {
            text.str = player.name;
            text.pos = player.pos;
            text.pos.y -= 6.f;

            textCount += writeTextToBuffer(text, font_, rects_ + textCount, getSize(rects_));
        }

        updateGLBuffers(glBuffers_, rects_, textCount);
        renderGLBuffers(glBuffers_, textCount);
    }


    // new round timer

    if(sim_.timeToStart_ > 0.f)
    {
        char buffer[20];
        Text text;
        text.str = buffer;
        snprintf(buffer, getSize(buffer), "%.3f", sim_.timeToStart_);
        text.color = {1.f, 0.5f, 1.f, 0.8f};
        text.scale = 2.f;
        text.pos = {(Simulation::MapSize * sim_.tileSize_ - getTextSize(text, font_).x)
                    / 2.f, 5.f};

        const int count = writeTextToBuffer(text, font_, rects_, getSize(rects_));

        uniform1i(program, "mode", FragmentMode::Font);
        bindTexture(font_.texture);
        updateGLBuffers(glBuffers_, rects_, count);
        renderGLBuffers(glBuffers_, count);
    }

    // score

    if(sim_.timeToStart_ > 0.f || showScore_)
    {
        char buffer[256];
        Text text;
        text.color = {1.f, 1.f, 0.f, 0.8f};
        text.scale = 0.8f;
        text.str = buffer;

        int bufOffset = 0;
        bufOffset += snprintf(buffer + bufOffset, max(0, getSize(buffer) - bufOffset),
                              "score:");

        for(const Player& player: sim_.players_)
        {
            bufOffset += snprintf(buffer + bufOffset, max(0, getSize(buffer) - bufOffset),
                                  "\n%d", player.score);
        }

        const vec2 textSize = getTextSize(text, font_);
        text.pos = ( vec2(Simulation::MapSize) * sim_.tileSize_ - textSize ) / 2.f;

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

        for(int i = 0; i < getSize(sim_.players_); ++i)
        {
            const Player& player = sim_.players_[i];
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
    ImGui::Text("controls:\n"
                "\n"
                "Esc       - display score\n"
                "\n"
                "player1:\n"
                "   WSAD   - move\n"
                "   C      - drop a bomb\n"
                "\n"
                "player2:\n"
                "   ARROWS - move\n"
                "   SPACE  - drop a bomb\n");

    ImGui::Spacing();

    if(ImGui::InputText("name", nameBuf_, sizeof(nameBuf_), ImGuiInputTextFlags_EnterReturnsTrue))
    {
        FILE* file;
        file = fopen(".name", "w");
        assert(file);
        fputs(nameBuf_, file);
        assert(!fclose(file));

        netcode::addMsg(netClient_.sendBuf, netcode::Cmd::Name, nameBuf_);
    }

    ImGui::Spacing();
    ImGui::Text("netcode::Client log");
    ImGui::InputTextMultiline("##netcode::Client log", netClient_.logBuf.data(),
        netClient_.logBuf.size(), ImVec2(300.f, 0.f), ImGuiInputTextFlags_ReadOnly);

    ImGui::End();
}
