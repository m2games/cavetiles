#include "Scene.hpp"
#include "imgui/imgui.h"
#include "GLFW/glfw3.h"
#include <math.h>

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
        if(particles[i].life <= 0.f)
        {
            particles[i] = particles[numActive - 1];
            rects[i] = rects[numActive - 1];
            --numActive;
        }
        
        rects[i].pos.x += particles[i].vel.x * dt;
        rects[i].pos.y += particles[i].vel.y * dt;
        particles[i].life -= dt;
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
            idx = 0;
    }
}

ivec2 getPlayerTile(const vec2 playerPos, const float tileSize)
{
    return {int(playerPos.x / tileSize + 0.5f),
            int(playerPos.y / tileSize + 0.5f)};
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
    glBuffers_ = createGLBuffers();
    tileTexture_ = createTextureFromFile("res/tiles.png");
    goblinTexture_ = createTextureFromFile("res/goblin.png");

    emitter_.spawn.pos = {tileSize_ * 11, tileSize_ * 8};
    emitter_.spawn.size = {5.f, 5.f};
    emitter_.spawn.hz = 100.f;
    emitter_.particleRanges.life = {3.f, 6.f};
    emitter_.particleRanges.size = {0.25f, 2.f};
    emitter_.particleRanges.vel = {{-3.5f, -30.f}, {3.5f, -2.f}};
    emitter_.particleRanges.color = {{0.1f, 0.f, 0.f, 0.f}, {0.5f, 0.25f, 0.f, 0.f}};
    emitter_.reserve();

    for (int i = 0; i < 10; i++)
    {
        for (int j = 0; j < 10; j++)
        {
            Rect& rect = rects_[j + i * 10];
            rect.pos = {j * tileSize_, i * tileSize_};
            rect.size = {tileSize_, tileSize_};

            switch (tiles_[i][j])
            {
                case 1: rect.texRect = {0.f, 0.f, 64.f, 64.f};   break;
                case 2: rect.texRect = {64.f, 0.f, 64.f, 64.f};  break;
                case 3: rect.texRect = {128.f, 0.f, 32.f, 32.f};
                        rect.color = {0.25f, 0.25f, 0.25f, 1.f}; break;
            }

            rect.texRect.x /= tileTexture_.size.x;
            rect.texRect.y /= tileTexture_.size.y;
            rect.texRect.z /= tileTexture_.size.x;
            rect.texRect.w /= tileTexture_.size.y;
        }
    }

    for(Player& player: players_)
    {
        player.vel = 80.f;
    }

    // specific configuration for each player

    for(int i = Dir::Up; i < Dir::Count; ++i)
    {
        Anim anim;
        anim.frameDt = 0.07f;
        anim.numFrames = 4;

        // tightly coupled to the goblin texture asset
        int y;
        switch(i)
        {
            case Dir::Up:    y = 2; break;
            case Dir::Down:  y = 0; break;
            case Dir::Left:  y = 3; break;
            case Dir::Right: y = 1;
        }

        for(int i = 0; i < anim.numFrames; ++i)
        {
            anim.frames[i] = {0.f + 64.f * i, 64.f * y, 64.f, 64.f};
        }

        for(Player& player: players_)
        {
            player.anims[i] = anim;
        }
    }

    for(Player& player: players_)
    {
        player.texture = &goblinTexture_;
    }

    players_[0].pos = {tileSize_ * 1, tileSize_ * 1};
    players_[0].prevDir = Dir::Down;

    players_[1].pos = {tileSize_ * 8, tileSize_ * 8};
    players_[1].prevDir = Dir::Left;

}

GameScene::~GameScene()
{
    deleteTexture(goblinTexture_);
    deleteTexture(tileTexture_);
    deleteGLBuffers(glBuffers_);
}

void GameScene::processInput(const Array<WinEvent>& events)
{
    // @TODO(matiTechno): replace with 'gaffer on games' technique
    frame_.time = min(frame_.time, 0.033f);

    for (const WinEvent& e: events)
    {
        if(e.type == WinEvent::Key)
        {
            const bool on = e.key.action != GLFW_RELEASE;

            switch(e.key.key)
            {
                case GLFW_KEY_W:     keys_[0].up = on;    break;
                case GLFW_KEY_S:     keys_[0].down = on;  break;
                case GLFW_KEY_A:     keys_[0].left = on;  break;
                case GLFW_KEY_D:     keys_[0].right = on; break;

                case GLFW_KEY_UP:    keys_[1].up = on;    break;
                case GLFW_KEY_DOWN:  keys_[1].down = on;  break;
                case GLFW_KEY_LEFT:  keys_[1].left = on;  break;
                case GLFW_KEY_RIGHT: keys_[1].right = on;
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
    }
}

void GameScene::update()
{
    emitter_.update(frame_.time);

    for(Player& player: players_)
    {
        player.pos.x += player.vel * frame_.time * dirVecs_[player.dir].x;
        player.pos.y += player.vel * frame_.time * dirVecs_[player.dir].y;
        player.anims[player.dir].update(frame_.time);

        const ivec2 playerTile = getPlayerTile(player.pos, tileSize_);
        bool collision = false;

        for(int i = -1; i < 2; ++i)
        {
            for(int j = -1; j < 2; ++j)
            {
                const ivec2 tile = {playerTile.x + i, playerTile.y + j};

                if(tiles_[tile.y][tile.x] != 1 && isCollision(player.pos, tile, tileSize_))
                {
                    collision = true;
                    goto end;
                }
            }
        }
end:
        if(collision)
        {
            const vec2 playerTilePos = {playerTile.x * tileSize_, playerTile.y * tileSize_};

            if(player.dir == Dir::Left || player.dir == Dir::Right)
            {
                player.pos.x = playerTilePos.x;
            }
            else
            {
                player.pos.y = playerTilePos.y;
            }

            const ivec2 targetTile = {int(playerTile.x + dirVecs_[player.dir].x),
                                      int(playerTile.y + dirVecs_[player.dir].y)};

            // important: y first, x second
            if(tiles_[targetTile.y][targetTile.x] == 1)
            {

                const vec2 slideVec = {playerTilePos.x - player.pos.x,
                                       playerTilePos.y - player.pos.y};

                const vec2 slideDir = normalize(slideVec);

                player.pos.x += player.vel * frame_.time * slideDir.x;
                player.pos.y += player.vel * frame_.time * slideDir.y;

                const vec2 newSlideVec = {playerTilePos.x - player.pos.x,
                                          playerTilePos.y - player.pos.y};

                if(dot(slideVec, newSlideVec) < 0.f)
                {
                    player.pos = playerTilePos;
                }
            }
        }
    }
}

void GameScene::render(const GLuint program)
{
    bindProgram(program);

    Camera camera;
    camera.pos = {0.f, 0.f};
    camera.size = {10 * tileSize_, 10 * tileSize_};
    camera = expandToMatchAspectRatio(camera, frame_.fbSize);
    uniform2f(program, "cameraPos", camera.pos);
    uniform2f(program, "cameraSize", camera.size);

    // 1) render the tilemap

    uniform1i(program, "mode", FragmentMode::Texture);
    bindTexture(tileTexture_);
    updateGLBuffers(glBuffers_, rects_, getSize(rects_));
    renderGLBuffers(glBuffers_, getSize(rects_));

    // 2) render the players
    
    for(Player& player: players_)
    {
        Rect rect;
        rect.size = {tileSize_, tileSize_};
        rect.pos = player.pos;

        const vec4 frame = player.dir ? player.anims[player.dir].getCurrentFrame() :
                                        player.anims[player.prevDir].getCurrentFrame();

        rect.texRect.x = frame.x / player.texture->size.x;
        rect.texRect.y = frame.y / player.texture->size.y;
        rect.texRect.z = frame.z / player.texture->size.x;
        rect.texRect.w = frame.w / player.texture->size.y;

        uniform1i(program, "mode", FragmentMode::Texture);
        bindTexture(*player.texture);
        updateGLBuffers(glBuffers_, &rect, 1);
        renderGLBuffers(glBuffers_, 1);
    }

    // 3) particles

    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    uniform1i(program, "mode", FragmentMode::Color);
    updateGLBuffers(glBuffers_, emitter_.rects.data(), emitter_.numActive);
    renderGLBuffers(glBuffers_, emitter_.numActive);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // 4) imgui

    ImGui::ShowDemoWindow();
    ImGui::Begin("options");
    ImGui::Text("test test test !!!");
    if(ImGui::Button("quit"))
        frame_.popMe = true;
    ImGui::End();
}
