#include "Scene.hpp"
#include "imgui/imgui.h"

GameScene::GameScene()
{
    glBuffers = createGLBuffers();
}

GameScene::~GameScene()
{
    deleteGLBuffers(glBuffers);
}

void GameScene::processInput(const Array<WinEvent>& events) {(void)events;}

void GameScene::update() {}

void GameScene::render(const GLuint program)
{
    time_ += frame_.time;

    Rect rect;
    rect.pos = {50.f, 50.f};
    rect.size = {300.f, 300.f};
    rect.color = {1.f, 0.f, 0.f, 1.f};
    rect.rotation = time_;

    fillGLRectBuffer(glBuffers.rectBo, &rect, 1);

    bindProgram(program);
    uniform1i(program, "mode", FragmentMode::Color);
    uniform2f(program, "cameraPos", 0.f, 0.f);
    uniform2f(program, "cameraSize", frame_.fbSize);
    renderGLBuffers(glBuffers.vao, 1);

    ImGui::ShowDemoWindow();

    ImGui::Begin("options");
    ImGui::Text("test test test !!!");
    if(ImGui::Button("quit"))
            frame_.popMe = true;
    ImGui::End();
}
