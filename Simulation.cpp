#include "Scene.hpp"
#include <netdb.h>
#include <string.h>
#include <stdio.h>
#include <stack>
#include <vector>
#include <queue>
#define INF 0x3f3f3f3f

// @ this souldn't be there but... (not intuitive)
namespace netcode
{

const char* getCmdStr(int cmd)
{
    switch(cmd)
    {
        case Cmd::Ping:         return "PING";
        case Cmd::Pong:         return "PONG";
        case Cmd::Chat:         return "CHAT";
        case Cmd::GameFull:     return "GAME_FULL";
        case Cmd::SetName:      return "SET_NAME";
        case Cmd::NameOk:       return "NAME_OK";
        case Cmd::MustRename:   return "MUST_RENAME";
        case Cmd::PlayerInput:  return "PLAYER_INPUT";
        case Cmd::Simulation:   return "SIMULATION";
        case Cmd::InitTileData: return "INIT_TILE_DATA";
        case Cmd::AddBot:       return "ADD_BOT";
        case Cmd::RemoveBot:    return "REMOVE_BOT";
    }
    assert(false);
}

const void* get_in_addr(const sockaddr* const sa)
{
    if(sa->sa_family == AF_INET)
        return &( ( (sockaddr_in*)sa )->sin_addr );

    return &( ( (sockaddr_in6*)sa )->sin6_addr );
}

void addMsg(Array<char>& buffer, int cmd, const char* payload)
{
    if(cmd)
    {
        const char* cmdStr = getCmdStr(cmd);
        int len = strlen(cmdStr) + strlen(payload) + 2; // ' ' + '\0'
        int prevSize = buffer.size();
        buffer.resize(prevSize + len);
        assert(snprintf(buffer.data() + prevSize, len, "%s %s", cmdStr, payload) == len - 1);
    }
    // special case for http response
    else
    {
        int len = strlen(payload) + 1;
        int prevSize = buffer.size();
        buffer.resize(prevSize + len);
        memcpy(buffer.data() + prevSize, payload, len);
    }
}

} // netcode

// static data definitions

const float Simulation::dropCooldown_ = 1.f;
const float Simulation::tileSize_ = 20.f;
const vec2  Simulation::dirVecs_[Dir::Count] = {{0.f, 0.f}, {0.f, -1.f}, {0.f, 1.f},
    {-1.f, 0.f}, {1.f, 0.f}};

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

Simulation::Simulation()
{
    assert(MapSize % 2);

    // tilemap edges

    for(int i = 0; i < MapSize; ++i)
    {
        tiles_[0][i] = 2;
        tiles_[MapSize - 1][i] = 2;
        tiles_[i][0] = 2;
        tiles_[i][MapSize - 1] = 2;
    }

    // tilemap pillars

    for(int i = 2; i < MapSize - 1; i += 2)
    {
        for(int j = 2; j < MapSize - 1; j += 2)
        {
            tiles_[j][i] = 2;
        }
    }
}

void Simulation::setNewGame()
{
    // @ set BotData to default state here if you want
    bombs_.clear();

    for(int i = 0; i < players_.maxSize(); ++i)
    {
        Player& player = players_[i];
        player.vel = 80.f;
        player.dropCooldown = 0.f;
        player.hp = HP;
        // so player.prevDir (***) won't be overwritten in processPlayerInput()
        player.dir = Dir::Nil;

        switch(i)
        {
            case 0:
                player.pos = vec2(tileSize_ * 1);
                player.prevDir = Dir::Right;
                break;

            case 1:
                player.pos = vec2(tileSize_ * (MapSize - 2));
                player.prevDir = Dir::Left;
                break;

            case 2:
                player.pos = vec2(tileSize_) * vec2(1, MapSize - 2);
                player.prevDir = Dir::Right;
                break;

            case 3:
                player.pos = vec2(tileSize_) * vec2(MapSize - 2, 1);
                player.prevDir = Dir::Down;
                break;

            default: assert(false);
        }
    }


    // tilemap crates

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

// Change these values if you change MapSize... ugly
ivec2 minDistance(int dist[][13], bool sptSet[][13], int MapSize) 
{ 
   // Initialize min value 
    int min = INT_MAX;
    ivec2 min_index; 
    for (int i = 0; i < MapSize; ++i)
    {
        for (int v = 0; v < MapSize; v++) 
        {
            if (sptSet[i][v] == false && dist[i][v] <= min) 
                min = dist[i][v], 
                min_index = {v, i}; 
        }
    }
   
    return min_index; 
}

void Simulation::updateAndProcessBotInput(const char* name, float dt)
{
    if(timeToStart_ > 0.f) // this is already checked in processPlayerInput()
        return;

    const Player* pptr = nullptr;

    for(Player& p: players_)
    {
        if(strcmp(name, p.name) == 0)
        {
            pptr = &p;
            break;
        }
    }

    assert(pptr);

    const Player& botPlayer = *pptr;

    if(botPlayer.hp == 0) // same as as timeToStart_
        return;

    BotData& botData = botData_[pptr - players_.begin()];
    Action action;

    botData.timerDrop += dt;
    botData.timerDir += dt;


    // Make the bot scan the game every 0.6 s
    if(botData.timerDir > 0.6f && botData.shortestPath.empty())
    {
        botData.timerDir = 0.f;

        // Getting state of the game, with danger zones...
        int gameState[MapSize][MapSize];
        memcpy(gameState, tiles_, sizeof(tiles_));
        int dummyRange;

        for(int bombIdx = 0; bombIdx < bombs_.size(); ++bombIdx)
        {
            Bomb bomb = bombs_[bombIdx];
            dummyRange = bomb.range + 2;
            gameState[bomb.tile.y][bomb.tile.x] = 3;

            for(int dirIdx = Dir::Nil; dirIdx < Dir::Count; ++dirIdx)
            {
                const vec2 dir = dirVecs_[dirIdx];
                const int range = (dirIdx != Dir::Nil) ? bomb.range : 1;

                for(int step = 1; step <= range; ++step)
                {
                    const ivec2 tile = bomb.tile + ivec2(dir) * step;
                    int tileValue = tiles_[tile.y][tile.x];

                    if (tileValue == 0) 
                    {
                        // Set tile to danger zone!
                        gameState[tile.y][tile.x] = 3;
                    }
                }
            }
        }
        // Copy the state of the game to the memory of bot
        memcpy(botData.gameState, gameState, sizeof(tiles_));
        
        // ******** STOP DANGER ZONE *********

        // START CHECKING IF IT SAFE
        const ivec2 underPlayerTile = getPlayerTile(botPlayer, tileSize_);
        if (gameState[underPlayerTile.y][underPlayerTile.x] == 3) 
        {   
            // Run for your life

            std::priority_queue<std::pair<int, int>, std::vector<std::pair<int, int>>, std::greater<std::pair<int, int>>> Q;
            std::vector<int> dist;
            std::vector<int> prev;
            int n = MapSize * MapSize;
            prev.assign(n, -1);
            dist.assign(n, INT_MAX);

            int source = underPlayerTile.y * MapSize + underPlayerTile.x;
            dist[source] = 0;
            Q.push({0, source});

            // Perform Dijkstra which returns shortes path tiles.
            while (!Q.empty())
            {
                int curr = Q.top().second;
                Q.pop();

                int currX = curr % MapSize;
                int currY = (curr - currX) / MapSize;
                int currVal = gameState[currY][currX];
                
                if (currVal == 0 || dist[curr] == 5) 
                {
                    // add to the path and break - we have found our safe place
                    // or the path got too long.
                    botData.shortestPath.push(curr); 
                    break;
                }

                // for each neighbour i.e tile in cross direction update dist...
                for(int dirIdx = Dir::Nil; dirIdx < Dir::Count; ++dirIdx) 
                {
                        const vec2 dir = dirVecs_[dirIdx];
                        int next;
                        if (dirIdx == Dir::Up || dirIdx == Dir::Down) next = curr + MapSize * dir.y;
                        else next = curr + dir.x;

                        int nextX = next % MapSize;
                        int nextY = (next - nextX) / MapSize;
                        const int nextTileVal = gameState[nextY][nextX];
                        
                        if (nextTileVal == 0 || nextTileVal == 3) 
                        {
                            int alt = dist[curr] + 1;
                            if (alt < dist[next]) 
                            {
                                dist[next] = alt;
                                prev[next] = curr;
                                Q.push({dist[next], next});
                            }
                        }
                }    
            }

              // Get the shortest path
            if (!botData.shortestPath.empty()) 
            {
                int targetTile = botData.shortestPath.top();
                int parentTile = prev[targetTile];
                targetTile = parentTile;
                
                while (targetTile != -1)
                {
                    botData.shortestPath.push(targetTile);
                    targetTile = prev[targetTile];
                }
            }
        }
    }
    
    // Execute path...
    if (botData.timerDir > 0.2f && !botData.shortestPath.empty())
    {
        botData.timerDir = 0.f;
        int nextTile = botData.shortestPath.top();
        botData.shortestPath.pop();
        int nextTileX = nextTile % MapSize;
        ivec2 nextMove = {nextTileX, ((nextTile - nextTileX) / MapSize)};
        ivec2 botPosition = getPlayerTile(botPlayer, tileSize_);
        ivec2 willGo = botPosition - nextMove;

        // Go to the corresponding direciton...
        if      (willGo.x == -1 && willGo.y == 0) botData.dir = Dir::Right;
        else if (willGo.x == 1 && willGo.y == 0) botData.dir = Dir::Left;
        else if (willGo.x == 0 && willGo.y == -1) botData.dir = Dir::Down;
        else if (willGo.x == 0 && willGo.y == 1) botData.dir = Dir::Up;
        else
        {
            // If something went wrong, try going to random neighbour...
            for (int dirIdx = Dir::Nil; dirIdx < Dir::Count; ++dirIdx)
            {
                const vec2 dir = dirVecs_[dirIdx];
                const ivec2 tile = botPosition + ivec2(dir);
                int tileValue = botData.gameState[tile.y][tile.x];
                if (tileValue == 0 || tileValue == 3) 
                {
                    botData.dir = dirIdx;
                    break;
                }
            }
        }
    } 
    // Attack! Change the first condition...
    // else if (botData.timerDir > 0.2f && botData.shortestPath.empty())
    // {   
    //     botData.timerDir = 0.f;
    //     botData.dir = getRandomInt(0, 4);
    // }
    
    
    // Drop the bomb every 5 s
    if(botData.timerDrop > 5.f)
    {
        botData.timerDrop = 0.f;
        action.drop = true;
    }

    switch(botData.dir)
    {
        case Dir::Up: action.up = true; break;
        case Dir::Down: action.down = true; break;
        case Dir::Right: action.right = true; break;
        case Dir::Left: action.left = true; break;
        default: break;
    }

    processPlayerInput(action, name);
}

void Simulation::processPlayerInput(const Action& action, const char* name)
{
    if(timeToStart_ > 0.f)
        return;

    Player* pptr = nullptr;

    for(Player& p: players_)
    {
        if(strcmp(name, p.name) == 0)
        {
            pptr = &p;
            break;
        }
    }

    assert(pptr);

    Player& player = *pptr;

    if(player.hp == 0)
        return;

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
                    const int playerIdx = &player - players_.begin();
                    bomb.addPlayer(playerIdx);
                }
            }

            bombs_.pushBack(bomb);
        }
    }
}

bool Simulation::update(float dt, FixedArray<ExploEvent, 50>& exploEvents)
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

    if(numLosers && numLosers >= players_.size() - 1)
    {
        for(Player& player: players_)
            player.score += player.hp > 0;

        timeToStart_ = 3.f;
        setNewGame();
        return true;
    }
    else
        return false;
}
