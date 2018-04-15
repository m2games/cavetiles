#include "glad.h"
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>

static void errorCallback(int error, const char* description)
{
    (void)error;
    printf("GLFW error: %s\n", description);
}

const char* const vertexSrc = R"(
#version 330

layout(location = 0) in vec2 vertex;

// instanced
layout(location = 1) in vec2 pos;
layout(location = 2) in vec2 size;
layout(location = 3) in vec4 color;
layout(location = 4) in float rotation;

uniform vec2 cameraPos;
uniform vec2 cameraSize;

out vec4 vColor;

void main()
{
    vColor = color;

    float r = length(vertex);
    float s = sin(rotation);
    float c = cos(rotation);

    vec2 vertexRot;
    vertexRot.x = vertex.x * c - vertex.y * s;
    // - because in our coordinate system y grows down
    vertexRot.y = -(vertex.x * s + vertex.y * c);

    //                              see static vbo buffer
    vec2 vertexWorld = (vertexRot + vec2(0.5)) * size + pos;
    vec2 vertexClip = (vertexWorld - cameraPos) * vec2(2.0) / cameraSize
        + vec2(-1.0);

    // in OpenGL y grows up, we have to do the flipping
    vertexClip.y *= -1.0;
    gl_Position = vec4(vertexClip, 0.0, 1.0);
}
)";

const char* const fragmentSrc = R"(
#version 330
in vec4 vColor;
out vec4 color;
void main()
{
    color = vColor;
}
)";

// returns true on error
static bool isCompileError(const GLuint shader)
{
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    
    if(success == GL_TRUE)
        return false;
    else
    {
        char buffer[512];
        glGetShaderInfoLog(shader, sizeof(buffer), nullptr, buffer);
        printf("glCompileShader() error:\n%s\n", buffer);
        return true;
    }
}

// returns 0 on failure
// program must be deleted with glDeleteProgram()
static GLuint createProgram(const char* const vertexSrc, const char* const fragmentSrc)
{
    const GLuint vertex = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex, 1, &vertexSrc, nullptr);
    glCompileShader(vertex);

    const GLuint fragment = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment, 1, &fragmentSrc, nullptr);
    glCompileShader(fragment);
    
    {
        const bool vertexError = isCompileError(vertex);
        const bool fragmentError = isCompileError(fragment);

        if(vertexError || fragmentError)
        {
            glDeleteShader(vertex);
            glDeleteShader(fragment);
            return 0;
        }
    }

    const GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    glDetachShader(program, vertex);
    glDetachShader(program, fragment);
    glDeleteShader(vertex);
    glDeleteShader(fragment);

    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    
    if(success == GL_TRUE)
        return program;
    else
    {
        glDeleteProgram(program);
        char buffer[512];
        glGetProgramInfoLog(program, sizeof(buffer), nullptr, buffer);
        printf("glLinkProgram() error:\n%s\n", buffer);
        return 0;
    }
}

struct vec2
{
    float x;
    float y;
};

struct vec4
{
    float x;
    float y;
    float z;
    float w;
};

struct Rect
{
    vec2 pos;
    vec2 size;
    vec4 color = {1.f, 1.f, 1.f, 1.f};
    float rotation = 0.f;
};

struct Camera
{
    vec2 pos;
    vec2 size;
};

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

    GLuint program = createProgram(vertexSrc, fragmentSrc);

    GLuint vao, vbo, vboInst;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &vboInst);

    float vertices[] = 
    {
        -0.5f, -0.5f,
        0.5f, -0.5f,
        0.5f, 0.5f,
        0.5f, 0.5f,
        -0.5f, 0.5f,
        -0.5f, -0.5f
    };

    // static buffer
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), &vertices, GL_STATIC_DRAW);

    glBindVertexArray(vao);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(0);

    // dynamic instanced buffer
    glBindBuffer(GL_ARRAY_BUFFER, vboInst);

    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glEnableVertexAttribArray(3);
    glEnableVertexAttribArray(4);
    glVertexAttribDivisor(1, 1);
    glVertexAttribDivisor(2, 1);
    glVertexAttribDivisor(3, 1);
    glVertexAttribDivisor(4, 1);

    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Rect), nullptr);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Rect),
            (const void*)offsetof(Rect, size));
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(Rect),
            (const void*)offsetof(Rect, color));
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(Rect),
            (const void*)offsetof(Rect, rotation));

    Rect rect;
    rect.pos = {20.f, 20.f};
    rect.size = {20.f, 20.f};
    rect.color = {0.f, 1.f, 0.45f, 1.f};

    Camera camera;
    camera.pos = {0.f, 0.f};
    camera.size = {100.f, 100.f};

    double time = glfwGetTime();

    while(!glfwWindowShouldClose(window))
    {
        double newTime = glfwGetTime();
        const float dt = newTime - time;
        time = newTime;

        glfwPollEvents();

        rect.rotation += dt;

        int width, height;
        glfwGetFramebufferSize(window, &width, &height);

        glViewport(0, 0, width, height);
        glClearColor(0.1f, 0.1f, 0.1f, 0.f);
        glClear(GL_COLOR_BUFFER_BIT);

        // adjust the camera to the aspect ratio
        {
            const float viewportAspect = float(width) / height;
            const float cameraAspect = camera.size.x / camera.size.y;

            vec2 size = camera.size;
            vec2 pos = camera.pos;

            if(viewportAspect > cameraAspect)
            {
                size.x = size.y * viewportAspect;
            }
            else if(viewportAspect < cameraAspect)
            {
                size.y = size.x / viewportAspect;
            }

            pos.x -= (size.x - camera.size.x) / 2.f;
            pos.y -= (size.y - camera.size.y) / 2.f;

            glUniform2f(glGetUniformLocation(program, "cameraPos"), pos.x, pos.y);
            glUniform2f(glGetUniformLocation(program, "cameraSize"), size.x, size.y);
        }


        glBindBuffer(GL_ARRAY_BUFFER, vboInst);
        glBufferData(GL_ARRAY_BUFFER, sizeof(Rect), &rect, GL_DYNAMIC_DRAW);

        glUseProgram(program);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 6, 1);

        glfwSwapBuffers(window);
    }

    glDeleteVertexArrays(1, &vao);
    glDeleteBuffers(1, &vbo);
    glDeleteBuffers(1, &vboInst);
    glDeleteProgram(program);

    glfwTerminate();
    return EXIT_SUCCESS;
}
