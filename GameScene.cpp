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
    player_.size = {70.f, 70.f};
    player_.color = {1.f, 0.f, 0.f, 1.f};
}

GameScene::~GameScene()
{
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
    // TODO Matbanero change it to the move() function.
    if (move_.R)
    {
        player_.pos.x += player_.vel * frame_.time;
    }
    if (move_.L)
    {
        player_.pos.x -= player_.vel * frame_.time;
    }
    if (move_.D)
    {
        player_.pos.y += player_.vel * frame_.time;
    }
    if (move_.U)
    {
        player_.pos.y -= player_.vel * frame_.time;
    }
    outOfBound(player_);
    isCollision(player_, rects_[25]);
}

void GameScene::render(const GLuint program)
{
    Rect& rect = rects_[100];
    rect.pos = player_.pos;
    rect.size = player_.size;
    rect.color = player_.color;
    updateGLBuffers(glBuffers_, rects_, getSize(rects_));
    bindProgram(program);

    Camera camera;
    camera.pos = {0.f, 0.f};
    camera.size = {10 * 100.f, 10 * 100.f};
    camera = expandToMatchAspectRatio(camera, frame_.fbSize);

    uniform1i(program, "mode", FragmentMode::Color);
    uniform2f(program, "cameraPos", camera.pos);
    uniform2f(program, "cameraSize", camera.size);
    renderGLBuffers(glBuffers_, getSize(rects_));

    ImGui::ShowDemoWindow();

    ImGui::Begin("options");
    ImGui::Text("test test test !!!");
    if(ImGui::Button("quit"))
            frame_.popMe = true;
    ImGui::End();
}

//Checks if there is collision between player and the object.
// TODO Matbanero - check only neighbour rectangles.
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

void Player::move()
{

}
