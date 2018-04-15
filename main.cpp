#include "glad.h"
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>

static void errorCallback(int error, const char* description)
{
    (void)error;
    printf("GLFW error: %s\n", description);
}

int main()
{
    glfwSetErrorCallback(errorCallback);

    if(!glfwInit())
        return EXIT_FAILURE;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWmonitor* const monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
    GLFWwindow* const window = glfwCreateWindow(mode->width, mode->height, "cavetiles", monitor, nullptr);

    if(!window)
    {
        glfwTerminate();
        return EXIT_FAILURE;
    }

    glfwMakeContextCurrent(window);
    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
    glfwSwapInterval(1);

    while(!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        int width, height;
        glfwGetFramebufferSize(window, &width, &height);

        glViewport(0, 0, width, height);
        glClearColor(1.f, 0.5f, 0.f, 0.f);
        glClear(GL_COLOR_BUFFER_BIT);

        glfwSwapBuffers(window);

    }

    glfwTerminate();
    return EXIT_SUCCESS;
}
