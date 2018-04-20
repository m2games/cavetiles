#include "Scene.hpp"
#include "imgui/imgui.h"

Rect rects[101];
Rect player1;
void outOfBound(Rect player);
bool isCollision(Rect player, Rect object);

// Initializing the map, with values indicating the type of a tile
int tiles[10][10] =
{
    {1, 1, 2, 2, 3, 1, 3, 2, 1, 1},
    {1, 1, 3, 1, 1, 2, 1, 2, 3, 3},
    {1, 2, 3, 3, 3, 2, 3, 2, 3, 3},
    {2, 1, 1, 2, 2, 2, 1, 1, 3, 2},
    {3, 1, 3, 3, 2, 3, 2, 2, 2, 3},
    {2, 2, 3, 3, 4, 2, 1, 3, 1, 2},
    {3, 2, 2, 3, 1, 3, 3, 3, 1, 1},
    {1, 2, 2, 3, 2, 3, 1, 2, 3, 3},
    {3, 3, 2, 2, 1, 2, 2, 2, 1, 3},
    {1, 2, 3, 3, 3, 2, 3, 3, 1, 1}
};

int rects_len;
bool moveR;
bool moveL;
bool moveU;
bool moveD;

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
            int color_type = tiles[i][j];
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
            rects[j + i * 10] = rect;
        }
    }

    player1.pos = {15.f, 15.f};
    player1.size = {70.f, 70.f};
    player1.color = {1.f, 0.f, 0.f, 1.f};
    rects[100] = player1;

    rects_len = sizeof(rects) / sizeof(rects[0]);
}

GameScene::~GameScene()
{
    deleteGLBuffers(glBuffers_);
}

void GameScene::processInput(const Array<WinEvent>& events)
{
    for (const WinEvent& e: events)
    {
        bool set = (e.key.action != GLFW_RELEASE);
        if (e.type == WinEvent::Key)
        {
            int k = e.key.key;
            set = (e.key.action != GLFW_RELEASE);

            switch (k)
            {
                case GLFW_KEY_RIGHT:
                    moveR = set;
                    break;
                case GLFW_KEY_LEFT:
                    moveL = set;
                    break;
                case GLFW_KEY_DOWN:
                    moveD = set;
                    break;
                case GLFW_KEY_UP:
                    moveU = set;
                    break;
                default:
                    break;
            }
        }
    }
}

void GameScene::update()
{
    // TODO Matbanero change it to the move() function.
    if (moveR)
    {
        player1.pos.x += 5;
    }
    if (moveL)
    {
        player1.pos.x -= 5;
    }
    if (moveD)
    {
        player1.pos.y += 5;
    }
    if (moveU)
    {
        player1.pos.y -= 5;
    }
    outOfBound(player1);
    isCollision(player1, rects[25]);
}

void GameScene::render(const GLuint program)
{

    updateGLBuffers(glBuffers_, rects, rects_len);
    bindProgram(program);

    Camera camera;
    camera.pos = {0.f, 0.f};
    camera.size = {10 * 100.f, 10 * 100.f};
    camera = expandToMatchAspectRatio(camera, frame_.fbSize);

    uniform1i(program, "mode", FragmentMode::Color);
    uniform2f(program, "cameraPos", camera.pos);
    uniform2f(program, "cameraSize", camera.size);
    renderGLBuffers(glBuffers_, rects_len);

    ImGui::ShowDemoWindow();

    ImGui::Begin("options");
    ImGui::Text("test test test !!!");
    if(ImGui::Button("quit"))
            frame_.popMe = true;
    ImGui::End();
}

//Checks if there is collision between player and the object.
// TODO Matbanero - check only neighbour rectangles.
bool isCollision(Rect player, Rect object)
{
    if (player.pos.x < object.pos.x + object.size.x
        && player.pos.x + player.size.x > object.pos.x
            && player.pos.y + player.size.y > object.pos.y
                && player.pos.y < object.pos.y + object.size.y)
    {
        return true;
    } else
    {
        return false;
    }

}

// Checks if the player is out of bounds, if so it stops
// from proceeding. Hardcoded for now.
void outOfBound(Rect player)
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
    rects[100] = player;
}
