#include "glad.h"
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include "imgui/imgui.h"
#include "imgui/imgui_impl_glfw_gl3.h"
#include "Array.hpp"
#include <cstring>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_RECT_PACK_IMPLEMENTATION
#include "imgui/stb_rect_pack.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include "imgui/stb_truetype.h"

#undef max
#define max(a,b) ((a) > (b) ? (a) : (b))

static void errorCallback(int error, const char* description)
{
    (void)error;
    printf("GLFW error: %s\n", description);
}

struct ivec2
{
    int x;
    int y;
};

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

struct Texture
{
    ivec2 size;
    GLuint id;
};

static void bindTexture(const Texture& texture, const GLuint unit = 0)
{
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, texture.id);
}

// delete with deleteTexture()
static Texture createDefaultTexture()
{
    Texture tex;
    glGenTextures(1, &tex.id);
    bindTexture(tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    tex.size = {1, 1};
    const unsigned char color[] = {0, 255, 0, 255};
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, tex.size.x, tex.size.y, 0,
            GL_RGBA, GL_UNSIGNED_BYTE, color);
    
    return tex;
}

// delete with deleteTexture()
static Texture createTextureFromFile(const char* const filename)
{
    Texture tex;
    glGenTextures(1, &tex.id);
    bindTexture(tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    unsigned char* const data = stbi_load(filename, &tex.size.x, &tex.size.y,
                                          nullptr, 4);

    if(!data)
    {
        printf("stbi_load() failed: %s\n", filename);
        tex.size = {1, 1};
        const unsigned char color[] = {0, 255, 0, 255};
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, tex.size.x, tex.size.y, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, color);
    }
    else
    {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, tex.size.x, tex.size.y, 0,
                GL_RGBA, GL_UNSIGNED_BYTE, data);

        stbi_image_free(data);
    }

    return tex;
}

static inline void deleteTexture(const Texture& texture)
{
    glDeleteTextures(1, &texture.id);
}

struct Glyph
{
    vec4 texRect;
    float advance;
    vec2 offset;
};

struct Font
{
    Texture texture;
    Glyph glyphs[127];
    float lineSpace;
};

// delete with deleteFont()
static Font createFontFromFile(const char* const filename, const int fontSize,
                               const int textureWidth)
{
    Font font;
    font.texture = createDefaultTexture();

    FILE* fp = fopen(filename, "rb");
    if(!fp)
    {
        printf("createFontFromFile() could not open file: %s\n", filename);
        return font;
    }

    Array<unsigned char> buffer;
    {
        fseek(fp, 0, SEEK_END);
        const int size = ftell(fp);
        rewind(fp);
        buffer.resize(size);
        fread(buffer.data(), sizeof(char), buffer.size(), fp);
        fclose(fp);
    }

    stbtt_fontinfo fontInfo;

    if(stbtt_InitFont(&fontInfo, buffer.data(), 0) == 0)
    {
        printf("stbtt_InitFont() failed: %s\n", filename);
        return font;
    }

    const float scale = stbtt_ScaleForPixelHeight(&fontInfo, fontSize);
    float ascent;

    {
        int descent, lineSpace, ascent_;
        stbtt_GetFontVMetrics(&fontInfo, &ascent_, &descent, &lineSpace);
        font.lineSpace = (ascent_ - descent + lineSpace) * scale;
        ascent = ascent_ * scale;
    }

    unsigned char* bitmaps[127];
    int maxBitmapSizeY = 0;
    ivec2 pos = {0, 0};
    for(int i = 32; i < 127; ++i)
    {
        const int idx = stbtt_FindGlyphIndex(&fontInfo, i);

        if(idx == 0)
        {
            printf("stbtt_FindGlyphIndex(%d) failed\n", i);
            continue;
        }

        Glyph& glyph = font.glyphs[i];
        int advance;
        int dummy;
        stbtt_GetGlyphHMetrics(&fontInfo, idx, &advance, &dummy);
        glyph.advance = advance * scale;

        ivec2 offset;
        ivec2 size;
        bitmaps[i] = stbtt_GetGlyphBitmap(&fontInfo, scale, scale, idx, &size.x, &size.y,
                                          &offset.x, &offset.y);

        glyph.offset.x = offset.x;
        glyph.offset.y = ascent + offset.y;
        glyph.texRect.z = size.x;
        glyph.texRect.w = size.y;

        if(pos.x + glyph.texRect.z > textureWidth)
        {
            pos.x = 0;
            pos.y += maxBitmapSizeY + 1;
            maxBitmapSizeY = 0;
        }

        glyph.texRect.x = pos.x;
        glyph.texRect.y = pos.y;

        pos.x += glyph.texRect.z + 1;
        maxBitmapSizeY = max(maxBitmapSizeY, glyph.texRect.w);
    }

    font.texture.size = {textureWidth, pos.y + maxBitmapSizeY};

    Array<unsigned char> clearBuf;
    clearBuf.resize(font.texture.size.x * font.texture.size.y);
    memset(clearBuf.data(), 0, clearBuf.size());

    GLint unpackAlignment;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpackAlignment);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    bindTexture(font.texture);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, font.texture.size.x, font.texture.size.y, 0,
                 GL_RED, GL_UNSIGNED_BYTE, clearBuf.data());

    for(int i = 32; i < 127; ++i)
    {
        const vec4& texRect = font.glyphs[i].texRect;
        glTexSubImage2D(GL_TEXTURE_2D, 0, texRect.x, texRect.y, texRect.z, texRect.w,
                        GL_RED, GL_UNSIGNED_BYTE, bitmaps[i]);
    }

    glPixelStorei(GL_UNPACK_ALIGNMENT, unpackAlignment);


    for(int i = 32; i < 127; ++i)
    {
        stbtt_FreeBitmap(bitmaps[i], nullptr);
    }

    return font;
}

static void deleteFont(Font& font)
{
    deleteTexture(font.texture);
}

const char* const vertexSrc = R"(
#version 330

layout(location = 0) in vec4 aVertex;

// instanced
layout(location = 1) in vec2 aiPos;
layout(location = 2) in vec2 aiSize;
layout(location = 3) in vec4 aiColor;
layout(location = 4) in vec4 aiTexRect;
layout(location = 5) in float aiRotation;

uniform vec2 cameraPos;
uniform vec2 cameraSize;

out vec4 vColor;
out vec2 vTexCoord;

void main()
{
    vColor = aiColor;
    vTexCoord = aVertex.zw * aiTexRect.zw + aiTexRect.xy;

    vec2 aPos = aVertex.xy;
    vec2 pos;

    // rotation
    float s = sin(aiRotation);
    float c = cos(aiRotation);
    pos.x = aPos.x * c - aPos.y * s;
    // -(...) because in our coordinate system y grows down
    pos.y = -(aPos.x * s + aPos.y * c);

    // convert to world coordinates
    //           see static vbo buffer
    pos = (pos + vec2(0.5)) * aiSize + aiPos;

    // convert to clip space
    pos = (pos - cameraPos) * vec2(2.0) / cameraSize
          + vec2(-1.0);
    // in OpenGL y grows up, we have to do the flipping
    pos.y *= -1.0;
    gl_Position = vec4(pos, 0.0, 1.0);
}
)";

struct FragmentMode
{
    enum
    {
        Color = 0,
        Texture = 1,
        Font = 2
    };
};

const char* const fragmentSrc = R"(
#version 330

in vec4 vColor;
in vec2 vTexCoord;

uniform sampler2D sampler;
uniform int mode = 0;

out vec4 color;

void main()
{
    color = vColor;

    if(mode == 1)
    {
        vec4 texColor = texture(sampler, vTexCoord);
        // premultiply alpha
        // texColor.rgb *= texColor.a;
        color *= texColor;
    }
    else if(mode == 2)
    {
        float alpha = texture(sampler, vTexCoord).r;
        color *= alpha;
    }
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
// program must be deleted with glDeleteProgram() (if != 0)
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

// @TODO(matiTechno)
// add origin for rotation (needed to properly rotate a text)
struct Rect
{
    vec2 pos;
    vec2 size;
    vec4 color = {1.f, 1.f, 1.f, 1.f};
    vec4 texRect = {0.f, 0.f, 1.f, 1.f};
    float rotation = 0.f;
};

struct Text
{
    vec2 pos;
    vec4 color = {1.f, 1.f, 1.f, 1.f};
    //float rotation = 0.f;
    float scale = 1.f;
    const char* str = "";
};

vec2 getTextSize(const Text& text, const Font& font)
{
    float x = 0.f;
    const float lineSpace = font.lineSpace * text.scale;
    vec2 size = {0.f, lineSpace};
    const int strLen = strlen(text.str);

    for(int i = 0; i < strLen; ++i)
    {
        const char c = text.str[i];

        if(c == '\n')
        {
            size.x = max(size.x, x);
            x = 0.f;
            size.y += lineSpace;
            continue;
        }

        //                               warning fix
        const Glyph& glyph = font.glyphs[int(c)];
        x += glyph.advance * text.scale;
    }
    
    size.x = max(size.x, x);
    return size;
}

struct GLBuffers
{
    GLuint vao;
    GLuint vbo;
    GLuint rectBo;
};

// delete with deleteGLBuffers()
GLBuffers createGLBuffers()
{
    GLBuffers glBuffers;
    glGenVertexArrays(1, &glBuffers.vao);
    glGenBuffers(1, &glBuffers.vbo);
    glGenBuffers(1, &glBuffers.rectBo);

    float vertices[] = 
    {
        -0.5f, -0.5f, 0.f, 1.f,
        0.5f, -0.5f, 1.f, 1.f,
        0.5f, 0.5f, 1.f, 0.f,
        0.5f, 0.5f, 1.f, 0.f,
        -0.5f, 0.5f, 0.f, 0.f,
        -0.5f, -0.5f, 0.f, 1.f
    };

    // static buffer
    glBindBuffer(GL_ARRAY_BUFFER, glBuffers.vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), &vertices, GL_STATIC_DRAW);

    glBindVertexArray(glBuffers.vao);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(0);

    // dynamic instanced buffer
    glBindBuffer(GL_ARRAY_BUFFER, glBuffers.rectBo);

    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glEnableVertexAttribArray(3);
    glEnableVertexAttribArray(4);
    glEnableVertexAttribArray(5);
    glVertexAttribDivisor(1, 1);
    glVertexAttribDivisor(2, 1);
    glVertexAttribDivisor(3, 1);
    glVertexAttribDivisor(4, 1);
    glVertexAttribDivisor(5, 1);

    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Rect), nullptr);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Rect),
                          (const void*)offsetof(Rect, size));
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(Rect),
                          (const void*)offsetof(Rect, color));
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(Rect),
                          (const void*)offsetof(Rect, texRect));
    glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, sizeof(Rect),
                          (const void*)offsetof(Rect, rotation));

    return glBuffers;
}

void fillGLRectBuffer(GLuint rectBo, const Rect* rects, int count)
{
    glBindBuffer(GL_ARRAY_BUFFER, rectBo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(Rect) * count, rects, GL_DYNAMIC_DRAW);
}

// call glUseProgram() first
void renderGLBuffer(GLuint vao, int numRects)
{
    glBindVertexArray(vao);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, numRects);
}

void deleteGLBuffers(GLBuffers& glBuffers)
{
    glDeleteVertexArrays(1, &glBuffers.vao);
    glDeleteBuffers(1, &glBuffers.vbo);
    glDeleteBuffers(1, &glBuffers.rectBo);
}

// returns the number of rects written
int writeTextToBuffer(const Text& text, const Font& font, Rect* buffer, const int maxSize)
{
    int count = 0;
    const char* str = text.str;
    vec2 penPos = text.pos;

    while(true)
    {
        const char c = *str;
        
        if(c == '\0')
            break;

        if(c == '\n')
        {
            penPos.x = text.pos.x;
            penPos.y += font.lineSpace * text.scale;
            ++str;
            continue;
        }

        //                               warning fix
        const Glyph& glyph = font.glyphs[int(c)];

        Rect& rect = buffer[count];
        // @TODO(matiTechno): vec2 add
        rect.pos.x = penPos.x + glyph.offset.x * text.scale;
        rect.pos.y = penPos.y + glyph.offset.y * text.scale;
        rect.size.x = glyph.texRect.z * text.scale;
        rect.size.y = glyph.texRect.w * text.scale;
        rect.color = text.color;
        rect.texRect.x = glyph.texRect.x / font.texture.size.x;
        rect.texRect.y = glyph.texRect.y / font.texture.size.y;
        rect.texRect.z = glyph.texRect.z / font.texture.size.x;
        rect.texRect.w = glyph.texRect.w / font.texture.size.y;
        rect.rotation = 0.f;

        ++count;
        assert(count <= maxSize);
        penPos.x += glyph.advance * text.scale;
        ++str;
    }

    return count;
}

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

    ImGui::CreateContext();
    ImGui_ImplGlfwGL3_Init(window, true);
    ImGui::StyleColorsDark();

    GLuint program = createProgram(vertexSrc, fragmentSrc);

    GLBuffers glBuffers = createGLBuffers();

    Rect rect;
    rect.pos = {20.f, 20.f};
    rect.size = {20.f, 20.f};
    rect.color = {0.f, 1.f, 0.45f, 1.f};

    Camera camera;
    camera.pos = {0.f, 0.f};
    camera.size = {100.f, 100.f};

    Texture texture = createTextureFromFile("res/github.png");

    Font font = createFontFromFile("res/Exo2-Black.otf", 30, 256);

    Text text;
    text.pos = {50.f, 50.f};
    text.str = "This is a demo text.\nMultiline!";

    Rect textRects[500];
    int numTextRects = writeTextToBuffer(text, font, textRects, 500);
    
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    double time = glfwGetTime();

    bool quit = false;
    while(!quit)
    {
        double newTime = glfwGetTime();
        const float dt = newTime - time;
        time = newTime;

        glfwPollEvents();
        quit = glfwWindowShouldClose(window);
        ImGui_ImplGlfwGL3_NewFrame();

        rect.rotation += dt;

        int fbWidth, fbHeight;
        glfwGetFramebufferSize(window, &fbWidth, &fbHeight);

        glViewport(0, 0, fbWidth, fbHeight);
        glClearColor(0.1f, 0.1f, 0.1f, 0.f);
        glClear(GL_COLOR_BUFFER_BIT);

        // adjust the camera to the aspect ratio
        const float viewportAspect = float(fbWidth) / fbHeight;
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

        glUseProgram(program);

        // rect
        {
            glUniform1i(glGetUniformLocation(program, "mode"), FragmentMode::Texture);
            glUniform2f(glGetUniformLocation(program, "cameraPos"), pos.x, pos.y);
            glUniform2f(glGetUniformLocation(program, "cameraSize"), size.x, size.y);

            bindTexture(texture);
            fillGLRectBuffer(glBuffers.rectBo, &rect, 1);
            glDrawArraysInstanced(GL_TRIANGLES, 0, 6, 1);
        }

        // font
        {
            glUniform1i(glGetUniformLocation(program, "mode"), FragmentMode::Font);
            glUniform2f(glGetUniformLocation(program, "cameraPos"), 0.f, 0.f);
            glUniform2f(glGetUniformLocation(program, "cameraSize"), fbWidth, fbHeight);

            fillGLRectBuffer(glBuffers.rectBo, textRects, numTextRects);
            bindTexture(font.texture);
            glDrawArraysInstanced(GL_TRIANGLES, 0, 6, numTextRects);
        }

        ImGui::Begin("options");
        ImGui::Text("dear imgui test!");
        if(ImGui::Button("quit"))
            quit = true;
        ImGui::End();

        ImGui::Render();
        ImGui_ImplGlfwGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    ImGui_ImplGlfwGL3_Shutdown();
    ImGui::DestroyContext();

    deleteFont(font);
    deleteTexture(texture);
    deleteGLBuffers(glBuffers);
    glDeleteProgram(program);

    glfwTerminate();
    return EXIT_SUCCESS;
}
