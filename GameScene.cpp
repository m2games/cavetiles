#include "Scene.hpp"
#include "imgui/imgui.h"

Rect rects[100];
// Initializing the map, with values indicating the type of a tile
int tiles[10][10] = {
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

GameScene::GameScene()
{
    glBuffers_ = createGLBuffers();

    // Initializing rectangles which make the map.
    for (int i = 0; i < 10; i++) {
        for (int j = 0; j < 10; j++) {
            Rect rect;
            rect.pos = {j * 100.f, i * 100.f};
            rect.size = {98.f, 98.f};

            // Get the type and set the visuals of the
            // tile from the tiles array.
            int color_type = tiles[i][j];
            switch (color_type) {
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
            }
            // Adding rectangle to the array, for later reference.
            rects[j + i * 10] = rect;
        }
    }

}

GameScene::~GameScene()
{
    deleteGLBuffers(glBuffers_);
}

void GameScene::processInput(const Array<WinEvent>& events) {(void)events;}

void GameScene::update() {}

void GameScene::render(const GLuint program)
{
    updateGLBuffers(glBuffers_, rects, 100);

    bindProgram(program);

    Camera camera;
    camera.pos = {0.f, 0.f};
    camera.size = {10 * 100.f, 10 * 100.f};
    camera = expandToMatchAspectRatio(camera, frame_.fbSize);

    uniform1i(program, "mode", FragmentMode::Color);
    uniform2f(program, "cameraPos", camera.pos);
    uniform2f(program, "cameraSize", camera.size);
    renderGLBuffers(glBuffers_, 100);

    ImGui::ShowDemoWindow();

    ImGui::Begin("options");
    ImGui::Text("test test test !!!");
    if(ImGui::Button("quit"))
            frame_.popMe = true;
    ImGui::End();
}
