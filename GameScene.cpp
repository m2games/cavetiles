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

    for (int i = 0; i < 10; i++)
    {
        for (int j = 0; j < 10; j++)
        {
            Rect& rect = rects_[j + i * 10];
            rect.pos = {j * tileSize_, i * tileSize_};
            rect.size = {tileSize_, tileSize_};

            switch (tiles_[i][j])
            {
                case 1: rect.color = {0.4f, 0.7f, 0.36f, 1.f};   break;
                case 2: rect.color = {0.3f, 0.f, 0.1f, 1.f};     break;
                case 3: rect.color = {0.24f, 0.24f, 0.24f, 1.f}; break;
                case 4: rect.color = {0.86f, 0.84f, 0.14f, 1.f};
            }
        }
    }

    for(int i = Dir::Up; i < Dir::Count; ++i)
    {
        Anim& anim = player_.anims[i];
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
    }

    player_.pos = {tileSize_ * 1, tileSize_ * 1};
    player_.vel = 80.f;
    player_.texture = createTextureFromFile("res/goblin.png");

    emitter_.spawn.pos = {tileSize_ * 11, tileSize_ * 8};
    emitter_.spawn.size = {5.f, 5.f};
    emitter_.spawn.hz = 100.f;
    emitter_.particleRanges.life = {3.f, 6.f};
    emitter_.particleRanges.size = {0.25f, 2.f};
    emitter_.particleRanges.vel = {{-5.f, -2.f}, {5.f, -30.f}};
    emitter_.particleRanges.color = {{0.1f, 0.f, 0.f, 0.f}, {0.5f, 0.25f, 0.f, 0.f}};
    emitter_.reserve();
}

GameScene::~GameScene()
{
    deleteTexture(player_.texture);
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
                case GLFW_KEY_UP:    keys_.up = on;   break;
                case GLFW_KEY_DOWN:  keys_.down = on; break;
                case GLFW_KEY_LEFT:  keys_.left = on; break;
                case GLFW_KEY_RIGHT: keys_.right = on;
            }
        }
    }

    if     (keys_.left)  player_.dir = Dir::Left;
    else if(keys_.right) player_.dir = Dir::Right;
    else if(keys_.up)    player_.dir = Dir::Up;
    else if(keys_.down)  player_.dir = Dir::Down;
    else                 player_.dir = Dir::Nil;
}

void GameScene::update()
{
    emitter_.update(frame_.time);

    player_.pos.x += player_.vel * frame_.time * dirVecs_[player_.dir].x;
    player_.pos.y += player_.vel * frame_.time * dirVecs_[player_.dir].y;
    player_.anims[player_.dir].update(frame_.time);

    const ivec2 playerTile = getPlayerTile(player_.pos, tileSize_);
    bool collision = false;

    for(int i = -1; i < 2; ++i)
    {
        for(int j = -1; j < 2; ++j)
        {
            const ivec2 tile = {playerTile.x + i, playerTile.y + j};

            if(tiles_[tile.y][tile.x] == 3 && isCollision(player_.pos, tile, tileSize_))
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

        if(player_.dir == Dir::Left || player_.dir == Dir::Right)
        {
            player_.pos.x = playerTilePos.x;
        }
        else
        {
            player_.pos.y = playerTilePos.y;
        }

        const ivec2 targetTile = {int(playerTile.x + dirVecs_[player_.dir].x),
                                  int(playerTile.y + dirVecs_[player_.dir].y)};

        // important: y first, x second
        if(tiles_[targetTile.y][targetTile.x] == 1)
        {

            const vec2 slideVec = {playerTilePos.x - player_.pos.x,
                                   playerTilePos.y - player_.pos.y};

            const vec2 slideDir = normalize(slideVec);

            player_.pos.x += player_.vel * frame_.time * slideDir.x;
            player_.pos.y += player_.vel * frame_.time * slideDir.y;

            const vec2 newSlideVec = {playerTilePos.x - player_.pos.x,
                                      playerTilePos.y - player_.pos.y};

            if(dot(slideVec, newSlideVec) < 0.f)
            {
                player_.pos = playerTilePos;
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

    updateGLBuffers(glBuffers_, rects_, getSize(rects_));
    uniform1i(program, "mode", FragmentMode::Color);
    renderGLBuffers(glBuffers_, getSize(rects_));

    // 2) render the player tile
    {
        Rect rect;
        rect.color = {1.f, 0.f, 0.f, 0.22f};
        rect.size = {tileSize_, tileSize_};

        const ivec2 tile = getPlayerTile(player_.pos, tileSize_);
        rect.pos.x = tile.x * tileSize_;
        rect.pos.y = tile.y * tileSize_;

        updateGLBuffers(glBuffers_, &rect, 1);
        renderGLBuffers(glBuffers_, 1);
    }

    // 3) render the player

    Rect rect;
    rect.pos = player_.pos;
    rect.size = {tileSize_, tileSize_};
    static vec4 frame = player_.anims[Dir::Down].frames[0];

    if(player_.dir != Dir::Nil)
    {
        frame = player_.anims[player_.dir].getCurrentFrame();
    }

    rect.texRect.x = frame.x / player_.texture.size.x;
    rect.texRect.y = frame.y / player_.texture.size.y;
    rect.texRect.z = frame.z / player_.texture.size.x;
    rect.texRect.w = frame.w / player_.texture.size.y;

    rect.color = {1.f, 0.f, 0.5f, 0.15f};
    updateGLBuffers(glBuffers_, &rect, 1);
    renderGLBuffers(glBuffers_, 1);

    rect.color = {1.f, 1.f, 1.f, 1.f};
    updateGLBuffers(glBuffers_, &rect, 1);
    uniform1i(program, "mode", FragmentMode::Texture);
    bindTexture(player_.texture);
    renderGLBuffers(glBuffers_, 1);

    // 4) particles

    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    updateGLBuffers(glBuffers_, emitter_.rects.data(), emitter_.numActive);
    uniform1i(program, "mode", FragmentMode::Color);
    renderGLBuffers(glBuffers_, emitter_.numActive);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // 5) imgui

    ImGui::ShowDemoWindow();
    ImGui::Begin("options");
    ImGui::Text("test test test !!!");
    if(ImGui::Button("quit"))
        frame_.popMe = true;
    ImGui::End();
}
