#include "Scene.hpp"
#include "imgui/imgui.h"
#include "GLFW/glfw3.h"

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
        anim.frameDt = 0.08f;
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
}

GameScene::~GameScene()
{
    deleteTexture(player_.texture);
    deleteGLBuffers(glBuffers_);
}

void GameScene::processInput(const Array<WinEvent>& events)
{
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

    if(keys_.up) player_.dir = Dir::Up;
    else if(keys_.down) player_.dir = Dir::Down;
    else if(keys_.left) player_.dir = Dir::Left;
    else if(keys_.right) player_.dir = Dir::Right;
    else player_.dir = Dir::Nil;
}

void GameScene::update()
{
    player_.pos.x += player_.vel * frame_.time * dirVecs_[player_.dir].x;
    player_.pos.y += player_.vel * frame_.time * dirVecs_[player_.dir].y;
    player_.anims[player_.dir].update(frame_.time);

    const ivec2 playerTile = getPlayerTile(player_.pos, tileSize_);

    for(int i = -1; i < 2; ++i)
    {
        for(int j = -1; j < 2; ++j)
        {
            const ivec2 tile = {playerTile.x + i, playerTile.y + j};

            if(tiles_[tile.y][tile.x] == 3)
            {
                if(isCollision(player_.pos, tile, tileSize_))
                {
                    if(player_.dir == Dir::Left || player_.dir == Dir::Right)
                        player_.pos.x = playerTile.x * tileSize_;

                    else 
                        player_.pos.y = playerTile.y * tileSize_;

                    goto end;
                }
            }
        }
    }
end:;
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

    // 4) imgui
    
    ImGui::ShowDemoWindow();
    ImGui::Begin("options");
    ImGui::Text("test test test !!!");
    if(ImGui::Button("quit"))
        frame_.popMe = true;
    ImGui::End();
}
