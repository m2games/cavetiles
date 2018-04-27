#include "Scene.hpp"
#include "imgui/imgui.h"
#include "GLFW/glfw3.h"
#include <math.h>
#include <stdio.h>

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

void Dynamite::addPlayer(const Player& player)
{
    for(const Player*& p: players)
    {
        assert(p != &player);
        if(p == nullptr)
        {
            p = &player;
            return;
        }
    }
    assert(false);
}

void Dynamite::removePlayer(const Player& player)
{
    for(const Player*& p: players)
    {
        if(p == &player)
        {
            p = nullptr;
            return;
        }
    }
    assert(false);
}

bool Dynamite::findPlayer(const Player& player) const
{
    for(const Player* const p: players)
    {
        if(p == &player)
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
        rects[i].pos.x += particles[i].vel.x * dt;
        rects[i].pos.y += particles[i].vel.y * dt;
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

        rect.size.x = getRandomFloat(particleRanges.size.min, particleRanges.size.max);
        rect.size.y = rect.size.x;

        rect.color.x = getRandomFloat(particleRanges.color.min.x, particleRanges.color.max.x);
        rect.color.y = getRandomFloat(particleRanges.color.min.y, particleRanges.color.max.y);
        rect.color.z = getRandomFloat(particleRanges.color.min.z, particleRanges.color.max.z);
        rect.color.w = getRandomFloat(particleRanges.color.min.w, particleRanges.color.max.w);

        rect.pos.x = getRandomFloat(0.f, 1.f) * spawn.size.x + spawn.pos.x
                     - rect.size.x / 2.f;
        rect.pos.y = getRandomFloat(0.f, 1.f) * spawn.size.y + spawn.pos.y
                     - rect.size.y / 2.f;

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
    return {int(player.pos.x / tileSize + 0.5f),
            int(player.pos.y / tileSize + 0.5f)};
}

bool isCollision(const vec2 playerPos, const ivec2 tile, const float tileSize)
{
    vec2 tilePos = {tile.x * tileSize, tile.y * tileSize};

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
    const float len = length(v);
    return {v.x / len, v.y / len};
}

float dot(const vec2 v1, const vec2 v2)
{
    return v1.x * v2.x + v1.y * v2.y;
}

GameScene::GameScene()
{
    assert(MapSize % 2);

    {
        Dynamite dynamite;
        assert(getSize(dynamite.players) == getSize(players_));
    }

    glBuffers_ = createGLBuffers();

    font_ = createFontFromFile("res/Exo2-Black.otf", 38, 512);

    textures_.tile = createTextureFromFile("res/tiles.png");
    textures_.player1 = createTextureFromFile("res/player1.png");
    textures_.player2 = createTextureFromFile("res/player2.png");
    textures_.dynamite = createTextureFromFile("res/bomb000.png");
    textures_.explosion = createTextureFromFile("res/Explosion.png");

    FCHECK( FMOD_System_CreateSound(fmodSystem, "res/sfx_exp_various6.wav",
                                    FMOD_CREATESAMPLE, nullptr, &sounds_.dynamite) );

    FCHECK( FMOD_System_CreateSound(fmodSystem, "res/sfx_exp_short_hard15.wav",
                                    FMOD_CREATESAMPLE, nullptr, &sounds_.crateExplosion) );

    emitter_.spawn.size = {5.f, 5.f};
    emitter_.spawn.pos = {210.f, 210.f};
    assert(emitter_.spawn.pos.x <= (MapSize - 1) * tileSize_);
    emitter_.spawn.hz = 100.f;
    emitter_.particleRanges.life = {3.f, 6.f};
    emitter_.particleRanges.size = {0.25f, 2.f};
    emitter_.particleRanges.vel = {{-3.5f, -30.f}, {3.5f, -2.f}};
    emitter_.particleRanges.color = {{0.1f, 0.f, 0.f, 0.f}, {0.5f, 0.25f, 0.f, 0.f}};
    emitter_.reserve();

    for(Player& player: players_)
    {
        player.vel = 80.f;
    }

    // specific configuration for each player
    assert(getSize(players_) == 2);

    players_[0].texture = &textures_.player1;
    players_[1].texture = &textures_.player2;

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
            players_[j].anims[i] = anim;
        }
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

GameScene::~GameScene()
{
    deleteGLBuffers(glBuffers_);
    deleteFont(font_);
    deleteTexture(textures_.tile);
    deleteTexture(textures_.player1);
    deleteTexture(textures_.player2);
    deleteTexture(textures_.dynamite);
    deleteTexture(textures_.explosion);
    FCHECK( FMOD_Sound_Release(sounds_.dynamite) );
    FCHECK( FMOD_Sound_Release(sounds_.crateExplosion) );
}

void GameScene::setNewGame()
{
    showScore_ = false;
    dynamites_.clear();

    for(Player& player: players_)
    {
        player.dropCooldown = 0.f;
        player.hp = HP;
        // so player.prevDir won't be overwritten in processInput()
        player.dir = Dir::Nil;
    }

    // specific configuration for each player
    assert(getSize(players_) == 2);

    players_[0].pos = {tileSize_ * 1, tileSize_ * 1};
    players_[0].prevDir = Dir::Down;

    players_[1].pos = {tileSize_ * (MapSize - 2), tileSize_ * (MapSize - 2)};
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
                const ivec2 playerTile = getPlayerTile(player, tileSize_);

                for(int k = -1; k < 2; ++k)
                {
                    for(int l = -1; l < 2; ++l)
                    {
                        const ivec2 adjacentTile = {playerTile.x + k, playerTile.y + l};

                        if(targetTile.x == adjacentTile.x && targetTile.y == adjacentTile.y)
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

void GameScene::processInput(const Array<WinEvent>& events)
{
    timeToStart_ -= frame_.time;

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

    const bool allowAction = timeToStart_ <= 0.f;

    // @TODO(matiTechno): replace with 'gaffer on games' technique
    frame_.time = min(frame_.time, 0.033f);

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
                    if(on && allowAction)
                        showScore_ = !showScore_;
                    break;
            }
        }
    }

    assert(getSize(players_) >= getSize(actions_));
    for(int i = 0; i < getSize(actions_); ++i)
    {
        if(players_[i].dir)
            players_[i].prevDir = players_[i].dir;

        if     (actions_[i].left  && allowAction) players_[i].dir = Dir::Left;
        else if(actions_[i].right && allowAction) players_[i].dir = Dir::Right;
        else if(actions_[i].up    && allowAction) players_[i].dir = Dir::Up;
        else if(actions_[i].down  && allowAction) players_[i].dir = Dir::Down;
        else                                      players_[i].dir = Dir::Nil;

        if(actions_[i].drop && allowAction)
        {
            const ivec2 targetTile = getPlayerTile(players_[i], tileSize_);
            bool freeTile = true;

            for(const Dynamite& dynamite: dynamites_)
            {
                if(dynamite.tile.x == targetTile.x && dynamite.tile.y == targetTile.y)
                {
                    freeTile = false;
                    break;
                }
            }

            if(freeTile && players_[i].dropCooldown == 0.f)
            {
                players_[i].dropCooldown = dropCooldown_;
                Dynamite dynamite;
                dynamite.tile = targetTile;
                dynamite.timer = 3.f;

                for(const Player& player: players_)
                {
                    if(isCollision(player.pos, targetTile, tileSize_))
                        dynamite.addPlayer(player);
                }

                dynamites_.pushBack(dynamite);
            }
        }

        actions_[i].drop = false;
    }
}

void GameScene::update()
{
    // emitter

    emitter_.update(frame_.time);

    // explosions

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

    // dynamites

    for(int dynIdx = 0; dynIdx < dynamites_.size(); ++dynIdx)
    {
        Dynamite& dynamite = dynamites_[dynIdx];
        dynamite.timer -= frame_.time;

        if(dynamite.timer > 0.f)
            continue;

        for(int dirIdx = Dir::Nil; dirIdx < Dir::Count; ++dirIdx)
        {
            const vec2 dir = dirVecs_[dirIdx];
            const int range = (dirIdx != Dir::Nil) ? dynamite.range : 1;

            for(int step = 1; step <= range; ++step)
            {
                const int x = dynamite.tile.x + int(dir.x) * step;
                const int y = dynamite.tile.y + int(dir.y) * step;
                int& tileValue = tiles_[y][x];

                if(tileValue == 2)
                    break;

                else if(tileValue == 1)
                {
                    tileValue = 0;

                    Explosion e;
                    e.anim = createExplosionAnim();
                    e.tile = {x, y};
                    e.size = tileSize_ * 2.f;
                    explosions_.pushBack(e);

                    playSound(sounds_.crateExplosion, 0.2f);
                    break;
                }
                else
                {
                    // @ shadowing
                    for(Dynamite& dynamite: dynamites_)
                    {
                        if(dynamite.tile.x == x && dynamite.tile.y == y)
                        {
                            // explode in the near future
                            dynamite.timer = min(dynamite.timer, 0.1f);
                        }
                    }

                    bool hit = false;
                    for(Player& player: players_)
                    {
                        const ivec2 playerTile = getPlayerTile(player, tileSize_);

                        if(playerTile.x == x && playerTile.y && playerTile.y == y && player.hp)
                        {
                            player.hp -= 1;
                            player.dmgTimer = 1.2f;
                            hit = true;
                        }
                    }

                    Explosion e;
                    e.anim = createExplosionAnim();
                    e.tile = {x, y};

                    if(hit)
                    {
                        e.size = tileSize_ * 2.f;
                        e.color = {1.f, 0.5f, 0.5f, 0.6f};
                    }
                    else
                    {
                        e.size = tileSize_ * 3.f;
                        e.anim.frameDt *= 1.5f;
                        e.color = {0.1f, 0.1f, 0.1f, 0.2f};
                    }

                    explosions_.pushBack(e);
                }
            }
        }
        dynamite = dynamites_.back();
        dynamites_.popBack();
        --dynIdx;
        playSound(sounds_.dynamite, 0.2f);
    }

    // players

    for(Player& player: players_)
    {
        player.pos.x += player.vel * frame_.time * dirVecs_[player.dir].x;
        player.pos.y += player.vel * frame_.time * dirVecs_[player.dir].y;
        player.anims[player.dir].update(frame_.time);
        player.dropCooldown -= frame_.time;
        player.dropCooldown = max(0.f, player.dropCooldown);
        player.dmgTimer -= frame_.time;

        // collisions
        // @TODO(matiTechno): unify collision code for tiles and dynamites?

        const ivec2 playerTile = getPlayerTile(player, tileSize_);

        // * with dynamites

        for(Dynamite& dynamite: dynamites_)
        {
            const bool collision = isCollision(player.pos, dynamite.tile, tileSize_);
            const bool allowed = dynamite.findPlayer(player);

            if(collision && !allowed)
            {
                if(player.dir == Dir::Left || player.dir == Dir::Right)
                {
                    player.pos.x = playerTile.x * tileSize_;
                }
                else
                {
                    player.pos.y = playerTile.y * tileSize_;
                }
            }
            else if(!collision && allowed)
            {
                dynamite.removePlayer(player);
            }
        }

        // * with tiles

        bool collision = false;

        for(int i = -1; i < 2; ++i)
        {
            for(int j = -1; j < 2; ++j)
            {
                const ivec2 tile = {playerTile.x + i, playerTile.y + j};

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
            if(player.dir == Dir::Left || player.dir == Dir::Right)
            {
                player.pos.x = playerTile.x * tileSize_;
            }
            else
            {
                player.pos.y = playerTile.y * tileSize_;
            }

            // sliding on the corners mechanic

            const vec2 offset = {player.pos.x - playerTile.x * tileSize_,
                                 player.pos.y - playerTile.y * tileSize_};

            ivec2 slideTile;

            if( (length(offset) > tileSize_ / 4.f) &&
                (tiles_[int(playerTile.y + dirVecs_[player.dir].y)]
                       [int(playerTile.x + dirVecs_[player.dir].x)] != 0) )
            {
                vec2 norm = normalize(offset);
                slideTile.x = playerTile.x + norm.x;
                slideTile.y = playerTile.y + norm.y;
                // @matiTechno
                assert(slideTile.x != playerTile.x || slideTile.y != playerTile.y);
            }
            else
                slideTile = playerTile;

            const vec2 slideTilePos = {slideTile.x * tileSize_, slideTile.y * tileSize_};

            // important: y first, x second
            // check if a tile next to slideTile (in the player direction) is free
            if(tiles_[int(slideTile.y + dirVecs_[player.dir].y)]
                     [int(slideTile.x + dirVecs_[player.dir].x)] == 0)
            {
                const vec2 slideVec = {slideTilePos.x - player.pos.x,
                                       slideTilePos.y - player.pos.y};

                const vec2 slideDir = normalize(slideVec);

                player.pos.x += player.vel * frame_.time * slideDir.x;
                player.pos.y += player.vel * frame_.time * slideDir.y;

                const vec2 newSlideVec = {slideTilePos.x - player.pos.x,
                                          slideTilePos.y - player.pos.y};

                if(dot(slideVec, newSlideVec) < 0.f)
                    player.pos = slideTilePos;
            }
        }
    }
}

void GameScene::render(const GLuint program)
{
    bindProgram(program);

    Camera camera;
    camera.pos = {0.f, 0.f};
    camera.size = {MapSize * tileSize_, MapSize * tileSize_};
    camera = expandToMatchAspectRatio(camera, frame_.fbSize);
    uniform2f(program, "cameraPos", camera.pos);
    uniform2f(program, "cameraSize", camera.size);

    // tilemap

    assert(MapSize * MapSize <= getSize(rects_));

    for (int i = 0; i < MapSize; ++i)
    {
        for (int j = 0; j < MapSize; ++j)
        {
            Rect& rect = rects_[j + i * MapSize];
            rect.pos = {j * tileSize_, i * tileSize_};
            rect.size = {tileSize_, tileSize_};
            rect.color = {1.f, 1.f, 1.f, 1.f};

            switch (tiles_[i][j])
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

    // dynamites

    assert(dynamites_.size() <= getSize(rects_));

    for(int i = 0; i < dynamites_.size(); ++i)
    {
        // @ somewhat specific to the dynamite texture asset
        const float coeff = fabs(sinf(dynamites_[i].timer * 2.f)) * 0.4f;
        Rect& rect = rects_[i];

        rect.size.x = tileSize_ + coeff * tileSize_;
        rect.size.y = rect.size.x;

        rect.pos = {dynamites_[i].tile.x * tileSize_ + (tileSize_ - rect.size.x) / 2.f,
                    dynamites_[i].tile.y * tileSize_ + (tileSize_ - rect.size.y) / 2.f};

        // default values might be overwritten
        rect.color = {1.f, 1.f, 1.f, 1.f};
        rect.texRect = {0.f, 0.f, 1.f, 1.f};
    }

    uniform1i(program, "mode", FragmentMode::Texture);
    bindTexture(textures_.dynamite);
    updateGLBuffers(glBuffers_, rects_, dynamites_.size());
    renderGLBuffers(glBuffers_, dynamites_.size());

    // players

    for(Player& player: players_)
    {
        Rect rect;
        rect.size = {tileSize_, tileSize_};
        rect.pos = player.pos;

        if(player.dmgTimer > 0.f)
            rect.color = {1.f, 0.2f, 0.2f, 0.3f};
        else
            rect.color = {1.f, 1.f, 1.f, 0.15f};

        uniform1i(program, "mode", FragmentMode::Color);
        updateGLBuffers(glBuffers_, &rect, 1);
        renderGLBuffers(glBuffers_, 1);

        rect.color = {1.f, 1.f, 1.f, 1.f};

        rect.texRect = player.dir ? player.anims[player.dir].getCurrentFrame() :
                                    player.anims[player.prevDir].frames[0];

        rect.texRect.x /= player.texture->size.x;
        rect.texRect.y /= player.texture->size.y;
        rect.texRect.z /= player.texture->size.x;
        rect.texRect.w /= player.texture->size.y;

        uniform1i(program, "mode", FragmentMode::Texture);
        bindTexture(*player.texture);
        updateGLBuffers(glBuffers_, &rect, 1);
        renderGLBuffers(glBuffers_, 1);
    }

    // explosions

    assert(explosions_.size() <= getSize(rects_));

    for(int i = 0; i < explosions_.size(); ++i)
    {
        rects_[i].size = {explosions_[i].size, explosions_[i].size};
        rects_[i].pos = {explosions_[i].tile.x * tileSize_ + (tileSize_ - rects_[i].size.x)
                                                           / 2.f,
                         explosions_[i].tile.y * tileSize_ + (tileSize_ - rects_[i].size.y)
                                                           / 2.f};
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
        for(const Player& player: players_)
        {
            // * hp
            rect[0].pos = player.pos;
            rect[0].size = {float(player.hp) / HP * tileSize_, h};
            rect[0].color = {1.f, 0.15f, 0.15f, 0.7f};

            // * drop cooldown
            rect[1].pos = rect[0].pos;
            rect[1].pos.y += h;
            rect[1].size = {player.dropCooldown / dropCooldown_ * tileSize_, h};
            rect[1].color = {1.f, 1.f, 0.f, 0.6f};

            rect += 2;
        }
    }

    uniform1i(program, "mode", FragmentMode::Color);
    updateGLBuffers(glBuffers_, rects_, getSize(players_) * 2.f);
    renderGLBuffers(glBuffers_, getSize(players_) * 2.f);

    // new round timer

    if(timeToStart_ > 0.f)
    {
        char buffer[20];
        Text text;
        text.str = buffer;
        snprintf(buffer, getSize(buffer), "%.3f", timeToStart_);
        text.color = {1.f, 0.5f, 1.f, 0.8f};
        text.scale = 2.f;
        const vec2 size = getTextSize(text, font_);
        text.pos = {(MapSize * tileSize_ - size.x) / 2.f, 5.f};

        const int count = writeTextToBuffer(text, font_, rects_, getSize(rects_));

        uniform1i(program, "mode", FragmentMode::Font);
        bindTexture(font_.texture);
        updateGLBuffers(glBuffers_, rects_, count);
        renderGLBuffers(glBuffers_, count);
    }

    // score

    if(timeToStart_ > 0.f || showScore_)
    {
        char buffer[256];
        Text text;
        text.color = {1.f, 1.f, 0.f, 0.8f};
        text.scale = 0.8f;
        text.str = buffer;

        int bufOffset = 0;
        bufOffset += snprintf(buffer + bufOffset, max(0, getSize(buffer) - bufOffset),
                              "score:");

        for(const Player& player: players_)
        {
            bufOffset += snprintf(buffer + bufOffset, max(0, getSize(buffer) - bufOffset),
                                  "\n%d", player.score);
        }

        const vec2 textSize = getTextSize(text, font_);
        text.pos = {(MapSize * tileSize_ - textSize.x) / 2.f,
                    (MapSize * tileSize_ - textSize.y) / 2.f};

        // * background
        {
            const float border = 5.f;
            Rect rect;
            rect.pos = {text.pos.x - border, text.pos.y - border};
            rect.size = {textSize.x + 2.f * border, textSize.y + 2.f * border};
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
        rect.size = {20.f, 20.f};
        rect.pos = {text.pos.x + textSize.x - rect.size.x, text.pos.y};

        for(const Player& player: players_)
        {
            rect.pos.y += lineSpace;
            rect.texRect = player.dir ? player.anims[player.dir].getCurrentFrame() :
                                        player.anims[player.prevDir].frames[0];

            rect.texRect.x /= player.texture->size.x;
            rect.texRect.y /= player.texture->size.y;
            rect.texRect.z /= player.texture->size.x;
            rect.texRect.w /= player.texture->size.y;

            bindTexture(*player.texture);
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
                "   C      - drop dynamite\n"
                "\n"
                "player2:\n"
                "   ARROWS - move\n"
                "   SPACE  - drop dynamite\n");
    ImGui::End();
}
