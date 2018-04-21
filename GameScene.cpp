#include "Scene.hpp"
#include "imgui/imgui.h"

void outOfBound(Player& player);
bool isCollision(Player& player, Rect object);

GameScene::GameScene()
{
    glBuffers_ = createGLBuffers();

    // Initializing rectangles which make the map.
    for (int i = 0; i < 10; i++)
    {
        for (int j = 0; j < 10; j++)
        {
            Rect rect;
            rect.pos = {j * 100.f, i * 100.f};
            rect.size = {98.f, 98.f};

            // Get the type and set the visuals of the
            // tile from the tiles array.
            int color_type = tiles_[i][j];
            switch (color_type)
            {
                case 1:
                    rect.color = {0.4f, 0.7f, 0.36f, 1.f};
                    break;
                case 2:
                    rect.color = {0.62f, 0.62f, 0.62f, 1.f};
                    break;
                case 3:
                    rect.color = {0.24f, 0.24f, 0.24f, 1.f};
                    break;
                case 4:
                    rect.color = {0.86f, 0.84f, 0.14f, 1.f};
                    break;
                default:
                    break;
            }
            // Adding rectangle to the array, for later reference.
            rects_[j + i * 10] = rect;
        }
    }

    player_.pos = {15.f, 15.f};
    player_.size = {100.f, 100.f};
    player_.color = {1.f, 0.f, 0.f, 1.f};
    player_.texture = createTextureFromFile("res/goblin.png");

    {
        Anim& anim = player_.anims.right;
        anim.frameDt = 0.08f;
        anim.numFrames = 4;

        for(int i = 0; i < anim.numFrames; ++i)
        {
            anim.frames[i] = {0.f + i * 64.f, 64.f, 64.f, 64.f};
        }
    }
    {
        Anim& anim = player_.anims.left;
        anim.frameDt = 0.08f;
        anim.numFrames = 4;

        for(int i = 0; i < anim.numFrames; ++i)
        {
            anim.frames[i] = {0.f + i * 64.f, 3 *64.f, 64.f, 64.f};
        }
    }
    {
        Anim& anim = player_.anims.up;
        anim.frameDt = 0.08f;
        anim.numFrames = 4;

        for(int i = 0; i < anim.numFrames; ++i)
        {
            anim.frames[i] = {0.f + i * 64.f, 2 * 64.f, 64.f, 64.f};
        }
    }
    {
        Anim& anim = player_.anims.down;
        anim.frameDt = 0.08f;
        anim.numFrames = 4;

        for(int i = 0; i < anim.numFrames; ++i)
        {
            anim.frames[i] = {0.f + i * 64.f, 0.f, 64.f, 64.f};
        }
    }
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
            bool set = (e.key.action != GLFW_RELEASE);

            switch(e.key.key)
            {
                case GLFW_KEY_RIGHT:
                    move_.R = set;
                    break;
                case GLFW_KEY_LEFT:
                    move_.L = set;
                    break;
                case GLFW_KEY_DOWN:
                    move_.D = set;
                    break;
                case GLFW_KEY_UP:
                    move_.U = set;
                    break;
            }
        }
    }
}

void GameScene::update()
{
    // Messy implementation of collision detection with neighbouring
    // objects TODO Matbanero improve it hardly.
    Rect rect;
    rect.pos = player_.pos;
    int playerIndex = getRectIndex(rect);

    if (move_.R)
    {
        player_.pos.x += player_.vel * frame_.time;
        player_.anims.right.update(frame_.time);

        if (isCollision(player_, rects_[playerIndex + 1]) &&
            getTile(rects_[playerIndex + 1]) != 1)
        {
            player_.pos.x = rects_[playerIndex + 1].pos.x - player_.size.x;
        }
    }
    else if (move_.L)
    {
        player_.pos.x -= player_.vel * frame_.time;
        player_.anims.left.update(frame_.time);

        // TODO inspect the problem with left direction Collision
        if (isCollision(player_, rects_[playerIndex - 1]) &&
            getTile(rects_[playerIndex - 1]) != 1)
        {
            player_.pos.x = rects_[playerIndex - 1].pos.x +
                rects_[playerIndex - 1].size.x + 1;
        }
    }
    else if (move_.D)
    {
        player_.pos.y += player_.vel * frame_.time;
        player_.anims.down.update(frame_.time);

        if (isCollision(player_, rects_[playerIndex + 10]) &&
            getTile(rects_[playerIndex + 10]) != 1)
        {
            player_.pos.y = rects_[playerIndex + 10].pos.y - player_.size.y;
        }
    }
    else if (move_.U)
    {
        player_.pos.y -= player_.vel * frame_.time;
        player_.anims.up.update(frame_.time);

        if (isCollision(player_, rects_[playerIndex - 10]) &&
            getTile(rects_[playerIndex - 10]) != 1)
        {
            player_.pos.y = rects_[playerIndex - 10].pos.y +
                rects_[playerIndex - 10].size.y;
        }
    }

    outOfBound(player_);

}

void GameScene::render(const GLuint program)
{
    updateGLBuffers(glBuffers_, rects_, getSize(rects_) - 1);
    bindProgram(program);

    Camera camera;
    camera.pos = {0.f, 0.f};
    camera.size = {10 * 100.f, 10 * 100.f};
    camera = expandToMatchAspectRatio(camera, frame_.fbSize);

    // render the tiles
    uniform1i(program, "mode", FragmentMode::Color);
    uniform2f(program, "cameraPos", camera.pos);
    uniform2f(program, "cameraSize", camera.size);
    renderGLBuffers(glBuffers_, getSize(rects_) - 1);

    // render the player

    {
        Rect rect;
        rect.pos = player_.pos;
        rect.size = player_.size;
        //rect.color = player_.color;
        rect.color = {1.f, 1.f, 1.f, 1.f};
        static vec4 frame = player_.anims.down.getCurrentFrame();

        if(move_.R) frame = player_.anims.right.getCurrentFrame();
        if(move_.U) frame = player_.anims.up.getCurrentFrame();
        if(move_.D) frame = player_.anims.down.getCurrentFrame();
        if(move_.L) frame = player_.anims.left.getCurrentFrame();

        rect.texRect.x = frame.x / player_.texture.size.x;
        rect.texRect.y = frame.y / player_.texture.size.y;
        rect.texRect.z = frame.z / player_.texture.size.x;
        rect.texRect.w = frame.w / player_.texture.size.y;

        updateGLBuffers(glBuffers_, &rect, 1);
    }

    uniform1i(program, "mode", FragmentMode::Texture);
    bindTexture(player_.texture);
    renderGLBuffers(glBuffers_, 1);

    ImGui::ShowDemoWindow();

    ImGui::Begin("options");
    ImGui::Text("test test test !!!");
    if(ImGui::Button("quit"))
            frame_.popMe = true;
    ImGui::End();
}

//Checks if there is collision between player and the object.
bool isCollision(Player& player, Rect object)
{
    return player.pos.x < object.pos.x + object.size.x &&
           player.pos.x + player.size.x > object.pos.x &&
           player.pos.y + player.size.y > object.pos.y &&
           player.pos.y < object.pos.y + object.size.y;
}

// Checks if the player is out of bounds, if so it stops
// from proceeding. Hardcoded for now.
void outOfBound(Player& player)
{
    if (player.pos.x < 0)
    {
        player.pos.x = 0;
    } else if (player.pos.x > 928)
    {
        player.pos.x = 928;
    }
    if (player.pos.y < 0)
    {
        player.pos.y = 0;
    } else if (player.pos.y > 928)
    {
        player.pos.y = 928;
    }
}

// Returns the index of the rect from rects_
int GameScene::getRectIndex(Rect rect)
{
    return rect.pos.x / 100 + rect.pos.y / 10;
}

// Returns the value of a tile
int GameScene::getTile(Rect rect)
{
    int j = rect.pos.x / 100;
    int i = rect.pos.y / 100;
    return tiles_[i][j];
}
