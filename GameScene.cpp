#include "Scene.hpp"
#include "imgui/imgui.h"
#include "GLFW/glfw3.h"
#include <math.h>

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
    {
        Dynamite dynamite;
        assert(getSize(dynamite.players) == getSize(players_));
    }

    glBuffers_ = createGLBuffers();

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
    assert(getSize(players_) >= 2);

    players_[0].pos = {tileSize_ * 1, tileSize_ * 1};
    players_[0].prevDir = Dir::Down;
    players_[0].texture = &textures_.player1;

    players_[1].pos = {tileSize_ * (MapSize - 2), tileSize_ * (MapSize - 2)};
    players_[1].prevDir = Dir::Left;
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

    // * crates

    int freeTiles[MapSize * MapSize];
    int numFreeTiles = 0;

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

GameScene::~GameScene()
{
    deleteGLBuffers(glBuffers_);
    deleteTexture(textures_.tile);
    deleteTexture(textures_.player1);
    deleteTexture(textures_.player2);
    deleteTexture(textures_.dynamite);
    deleteTexture(textures_.explosion);
    FCHECK( FMOD_Sound_Release(sounds_.dynamite) );
    FCHECK( FMOD_Sound_Release(sounds_.crateExplosion) );
}

void GameScene::processInput(const Array<WinEvent>& events)
{
    // @TODO(matiTechno): replace with 'gaffer on games' technique
    frame_.time = min(frame_.time, 0.033f);

    for(const WinEvent& e: events)
    {
        if(e.type == WinEvent::Key && e.key.action != GLFW_REPEAT)
        {
            const bool on = e.key.action == GLFW_PRESS;

            switch(e.key.key)
            {
                case GLFW_KEY_W:     keys_[0].up = on;    break;
                case GLFW_KEY_S:     keys_[0].down = on;  break;
                case GLFW_KEY_A:     keys_[0].left = on;  break;
                case GLFW_KEY_D:     keys_[0].right = on; break;
                case GLFW_KEY_C:     keys_[0].drop = on;  break;

                case GLFW_KEY_UP:    keys_[1].up = on;    break;
                case GLFW_KEY_DOWN:  keys_[1].down = on;  break;
                case GLFW_KEY_LEFT:  keys_[1].left = on;  break;
                case GLFW_KEY_RIGHT: keys_[1].right = on; break;
                case GLFW_KEY_SPACE: keys_[1].drop = on;  break;
            }
        }
    }

    assert(getSize(players_) >= getSize(keys_));
    for(int i = 0; i < getSize(keys_); ++i)
    {
        if(players_[i].dir)
            players_[i].prevDir = players_[i].dir;

        if     (keys_[i].left)  players_[i].dir = Dir::Left;
        else if(keys_[i].right) players_[i].dir = Dir::Right;
        else if(keys_[i].up)    players_[i].dir = Dir::Up;
        else if(keys_[i].down)  players_[i].dir = Dir::Down;
        else                    players_[i].dir = Dir::Nil;

        if(keys_[i].drop)
        {
            keys_[i].drop = false;

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

            if(freeTile && players_[i].dropCooldown <= 0.f)
            {
                players_[i].dropCooldown = 0.5f;
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
    }
}

void GameScene::update()
{
    // * emitter

    emitter_.update(frame_.time);

    // * explosions

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

    // *dynamites

    for(int dynIdx = 0; dynIdx < dynamites_.size(); ++ dynIdx)
    {
        Dynamite& dynamite = dynamites_[dynIdx];
        dynamite.timer -= frame_.time;

        if(dynamite.timer <= 0.f)
        {
            for(int dirIdx = Dir::Up; dirIdx < Dir::Count; ++dirIdx)
            {
                vec2 dir = dirVecs_[dirIdx];
                for(int step = 1; step <= dynamite.range; ++step)
                {
                    int i = dynamite.tile.x + int(dir.x) * step;
                    int j = dynamite.tile.y + int(dir.y) * step;
                    int& tile = tiles_[j][i];

                    if(tile == 1)
                    {
                        tile = 0;

                        Explosion explo;
                        explo.size = tileSize_ * 2.f;
                        explo.tile = {i, j};
                        explo.anim.frameDt = 0.08f;
                        explo.anim.numFrames = 12;

                        for(int i = 0 ; i < explo.anim.numFrames; ++i)
                            explo.anim.frames[i] = {i * 96.f, 0.f, 96.f, 96.f};

                        explosions_.pushBack(explo);
                        playSound(sounds_.crateExplosion, 0.2f);
                        break;
                    }
                    else if(tile == 2)
                        break;
                }
            }

            dynamite = dynamites_.back();
            dynamites_.popBack();
            --dynIdx;
            playSound(sounds_.dynamite, 0.2f);
        }

    }

    // * players

    for(Player& player: players_)
    {
        player.pos.x += player.vel * frame_.time * dirVecs_[player.dir].x;
        player.pos.y += player.vel * frame_.time * dirVecs_[player.dir].y;
        player.anims[player.dir].update(frame_.time);
        player.dropCooldown -= frame_.time;

        // collisions

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

    // * tilemap

    assert(MapSize * MapSize <= getSize(rects_));

    for (int i = 0; i < MapSize; ++i)
    {
        for (int j = 0; j < MapSize; ++j)
        {
            Rect& rect = rects_[j + i * MapSize];
            rect.pos = {j * tileSize_, i * tileSize_};
            rect.size = {tileSize_, tileSize_};

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

    // * dynamites

    assert(dynamites_.size() <= getSize(rects_));

    for(int i = 0; i < dynamites_.size(); ++i)
    {
        rects_[i].size = {tileSize_, tileSize_};
        rects_[i].pos = {dynamites_[i].tile.x * tileSize_, dynamites_[i].tile.y * tileSize_};
        // @ we have to reset color and texRect
        rects_[i].color = {1.f, 1.f, 1.f, 1.f};
        rects_[i].texRect = {0.f, 0.f, 1.f, 1.f};
    }

    uniform1i(program, "mode", FragmentMode::Texture);
    bindTexture(textures_.dynamite);
    updateGLBuffers(glBuffers_, rects_, dynamites_.size());
    renderGLBuffers(glBuffers_, dynamites_.size());

    // * players

    for(Player& player: players_)
    {
        Rect rect;
        rect.size = {tileSize_, tileSize_};
        rect.pos = player.pos;
        rect.color = {1.f, 1.f, 1.f, 0.2f};

        uniform1i(program, "mode", FragmentMode::Color);
        updateGLBuffers(glBuffers_, &rect, 1);
        renderGLBuffers(glBuffers_, 1);

        rect.color.w = 1.f;

        const vec4 frame = player.dir ? player.anims[player.dir].getCurrentFrame() :
                                        player.anims[player.prevDir].frames[0];

        rect.texRect.x = frame.x / player.texture->size.x;
        rect.texRect.y = frame.y / player.texture->size.y;
        rect.texRect.z = frame.z / player.texture->size.x;
        rect.texRect.w = frame.w / player.texture->size.y;

        uniform1i(program, "mode", FragmentMode::Texture);
        bindTexture(*player.texture);
        updateGLBuffers(glBuffers_, &rect, 1);
        renderGLBuffers(glBuffers_, 1);
    }

    // * explosions

    assert(explosions_.size() <= getSize(rects_));

    for(int i = 0; i < explosions_.size(); ++i)
    {
        rects_[i].size = {explosions_[i].size, explosions_[i].size};
        rects_[i].pos = {explosions_[i].tile.x * tileSize_ + (tileSize_ - rects_[i].size.x)
                                                           / 2.f,
                         explosions_[i].tile.y * tileSize_ + (tileSize_ - rects_[i].size.y)
                                                           / 2.f};
        rects_[i].color = {1.f, 1.f, 1.f, 1.f};
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

    // * particles

    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    uniform1i(program, "mode", FragmentMode::Color);
    updateGLBuffers(glBuffers_, emitter_.rects.data(), emitter_.numActive);
    renderGLBuffers(glBuffers_, emitter_.numActive);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // * imgui

    ImGui::ShowDemoWindow();
    ImGui::Begin("options");
    ImGui::Text("test test test !!!");
    if(ImGui::Button("quit"))
        frame_.popMe = true;
    ImGui::End();
}
