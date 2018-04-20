#include "glad.h"
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include "imgui/imgui.h"
//#include "imgui/imgui_impl_glfw_gl3.h"
#include "Array.hpp"
#include <string.h>
#include "Scene.hpp"
#include "fmod/fmod_errors.h"

// unity build
#include "GameScene.cpp"
#include "glad.c"
#include "imgui/imgui.cpp"
#include "imgui/imgui_demo.cpp"
#include "imgui/imgui_draw.cpp"
#include "imgui/imgui_impl_glfw_gl3.cpp"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_RECT_PACK_IMPLEMENTATION
#include "imgui/stb_rect_pack.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include "imgui/stb_truetype.h"

// @TODO(matiTechno): inline?

static GLint getUniformLocation(GLuint program, const char* const name)
{
    GLint loc = glGetUniformLocation(program, name);
    if(loc == -1)
        printf("program = %u: unfiform '%s' is inactive\n", program, name);
    return loc;
}

void uniform1i(const GLuint program, const char* const name, const int i)
{
    glUniform1i(getUniformLocation(program, name), i);
}

void uniform1f(const GLuint program, const char* const name, const float f)
{
    glUniform1f(getUniformLocation(program, name), f);
}

void uniform2f(const GLuint program, const char* const name, const float f1, const float f2)
{
    glUniform2f(getUniformLocation(program, name), f1, f2);
}

void uniform2f(const GLuint program, const char* const name, const vec2 v)
{
    glUniform2f(getUniformLocation(program, name), v.x, v.y);
}

// @TODO(matiTechno): vec3 Type + uniform3f(vec3)?

void uniform3f(const GLuint program, const char* const name, const float f1, const float f2,
               const float f3)
{
    glUniform3f(getUniformLocation(program, name), f1, f2, f3);
}

void uniform4f(const GLuint program, const char* const name, const float f1, const float f2,
               const float f3, const float f4)
{
    glUniform4f(getUniformLocation(program, name), f1, f2, f3, f4);
}

void uniform4f(const GLuint program, const char* const name, const vec4 v)
{
    glUniform4f(getUniformLocation(program, name), v.x, v.y, v.z, v.w);
}

static void errorCallback(const int error, const char* const description)
{
    (void)error;
    printf("GLFW error: %s\n", description);
}

void bindTexture(const Texture& texture, const GLuint unit)
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
Texture createTextureFromFile(const char* const filename)
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

void deleteTexture(const Texture& texture)
{
    glDeleteTextures(1, &texture.id);
}

// delete with deleteFont()
Font createFontFromFile(const char* const filename, const int fontSize, const int textureWidth)
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

void deleteFont(Font& font)
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
// program must be deleted with deleteProgram() (if != 0)
GLuint createProgram(const char* const vertexSrc, const char* const fragmentSrc)
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

void deleteProgram(const GLuint program)
{
    glDeleteProgram(program);
}

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

void updateGLBuffers(GLBuffers& glBuffers, const Rect* const rects, const int count)
{
    glBindBuffer(GL_ARRAY_BUFFER, glBuffers.rectBo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(Rect) * count, rects, GL_DYNAMIC_DRAW);
}

// @TODO(matiTechno): do we need these?
void bindProgram(const GLuint program)
{
    glUseProgram(program);
}

// call bindProgram() first
void renderGLBuffers(GLBuffers& glBuffers, const int numRects)
{
    glBindVertexArray(glBuffers.vao);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, numRects);
}

void deleteGLBuffers(GLBuffers& glBuffers)
{
    glDeleteVertexArrays(1, &glBuffers.vao);
    glDeleteBuffers(1, &glBuffers.vbo);
    glDeleteBuffers(1, &glBuffers.rectBo);
}

// returns the number of rects written
int writeTextToBuffer(const Text& text, const Font& font, Rect* const buffer,
                      const int maxSize)
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

static Array<WinEvent>* eventsPtr;

static void keyCallback(GLFWwindow* const window, const int key, const int scancode,
                             const int action, const int mods)
{
    WinEvent e;
    e.type = WinEvent::Key;
    e.key.key = key;
    e.key.action = action;
    e.key.mods = mods;
    eventsPtr->pushBack(e);

    ImGui_ImplGlfw_KeyCallback(window, key, scancode, action, mods);
}

static void cursorPosCallback(GLFWwindow*, const double xpos, const double ypos)
{
    WinEvent e;
    e.type = WinEvent::Cursor;
    e.cursor.pos.x = xpos;
    e.cursor.pos.y = ypos;
    eventsPtr->pushBack(e);
}

static void mouseButtonCallback(GLFWwindow* const window, const int button, const int action,
                                const int mods)
{
    WinEvent e;
    e.type = WinEvent::MouseButton;
    e.mouseButton.button = button;
    e.mouseButton.action = action;
    e.mouseButton.mods = mods;
    eventsPtr->pushBack(e);

    ImGui_ImplGlfw_MouseButtonCallback(window, button, action, mods);
}

static void scrollCallback(GLFWwindow* const window, const double xoffset,
                           const double yoffset)
{
    WinEvent e;
    e.type = WinEvent::Scroll;
    e.scroll.offset.x = xoffset;
    e.scroll.offset.y = yoffset;
    eventsPtr->pushBack(e);

    ImGui_ImplGlfw_ScrollCallback(window, xoffset, yoffset);
}

static void charCallback(GLFWwindow* const window, const unsigned int codepoint)
{
    ImGui_ImplGlfw_CharCallback(window, codepoint);
}

class LogoScene: public Scene
{
public:
    LogoScene()
    {
        FCHECK( FMOD_System_CreateSound(fmodSystem, "res/sfx_sound_vaporizing.wav",
                                        FMOD_CREATESAMPLE, nullptr, &sound_) );
        
        FMOD_CHANNEL* channel;
        FCHECK( FMOD_System_PlaySound(fmodSystem, sound_, nullptr, false, &channel) );
        FCHECK( FMOD_Channel_SetVolume(channel, 0.1f) );

        glBuffers_ = createGLBuffers();
        texture_ = createTextureFromFile("res/github.png");
        font_ = createFontFromFile("res/Exo2-Black.otf", 38, 512);

        Text text;
        text.scale = 0.9f;
        text.str = "m2games";
        text.color = {1.f, 1.f, 1.f, 0.7f};
        const vec2 size = getTextSize(text, font_);
        text.pos.x = (100.f - size.x) / 2.f;
        text.pos.y = (100.f - size.y) / 2.f;

        name_.numRects = writeTextToBuffer(text, font_, name_.rects, getSize(name_.rects));

        for(int i = 0; i < name_.numRects; ++i)
            name_.rects[i].pos.y -= name_.offset;
    }

    ~LogoScene() override
    {
        deleteGLBuffers(glBuffers_);
        deleteTexture(texture_);
        deleteFont(font_);
        FCHECK( FMOD_Sound_Release(sound_) );
    }
    
    void processInput(const Array<WinEvent>& events) override
    {
        for(const WinEvent& e: events)
        {
            if(e.type == WinEvent::Key && e.key.action == GLFW_PRESS)
            {
                int k = e.key.key;
                if(k == GLFW_KEY_SPACE || k == GLFW_KEY_ESCAPE || k == GLFW_KEY_ENTER)
                    time_ += 666.f;
            }
        }
    }

    void render(GLuint program) override
    {
        time_ += frame_.time;

        if(time_ > name_.time + 1.f)
        {
            frame_.popMe = true;
            frame_.newScene = new GameScene;
        }

        bindProgram(program);

        // first render some text in the pixel / viewport coordinates
        {
            Rect rects[50];

            Text text;
            text.pos = {50.f, 50.f};
            text.color = {1.f, 0.5f, 1.f, 1.f};
            text.str = "press ENTER / ESC / SPACE to skip";

            int count = writeTextToBuffer(text, font_, rects, getSize(rects));
            updateGLBuffers(glBuffers_, rects, count);

            uniform2f(program, "cameraPos", 0.f, 0.f);
            uniform2f(program, "cameraSize", frame_.fbSize);
            uniform1i(program, "mode", FragmentMode::Font);

            bindTexture(font_.texture);

            renderGLBuffers(glBuffers_, count);
        }

        // from here we will use the virtual world coordinates to render the scene
        Camera camera;
        camera.pos = {0.f, 0.f};
        camera.size = {100.f, 100.f};
        camera = expandToMatchAspectRatio(camera, frame_.fbSize);
        uniform2f(program, "cameraPos", camera.pos);
        uniform2f(program, "cameraSize", camera.size);

        // rect
        {
            Rect rect;
            rect.pos = {10.f, 50.f};
            rect.size = {20.f, 20.f};
            rect.color = {0.f, 1.f, 0.4f, 1.f};
            rect.rotation = time_ / 2.f;
            
            updateGLBuffers(glBuffers_, &rect, 1);
            uniform1i(program, "mode", FragmentMode::Texture);
            bindTexture(texture_);
            renderGLBuffers(glBuffers_, 1);
        }

        // animated studio name
        {
            if(name_.animationIdx < name_.numRects)
            {
                const float advanceTime = name_.time / name_.numRects;
                const float speed = name_.offset / advanceTime;

                name_.accumulator += frame_.time;
                name_.rects[name_.animationIdx].pos.y += speed * frame_.time;
                
                if(name_.accumulator >= advanceTime)
                {
                    name_.accumulator -= advanceTime;
                    name_.rects[name_.animationIdx].pos.y -= speed * name_.accumulator;
                    name_.rects[name_.animationIdx].color.z = 0.f;

                    ++name_.animationIdx;

                    if(name_.animationIdx < name_.numRects)
                        name_.rects[name_.animationIdx].pos.y += speed * name_.accumulator;
                }
            }

            updateGLBuffers(glBuffers_, name_.rects, name_.numRects);
            uniform1i(program, "mode", FragmentMode::Font);
            bindTexture(font_.texture);
            renderGLBuffers(glBuffers_, name_.numRects);
        }
    }

private:
    float time_ = 0.f;
    GLBuffers glBuffers_;
    Texture texture_;
    Font font_;
    FMOD_SOUND* sound_;

    struct
    {
        int animationIdx = 0;
        float accumulator = 0.f;

        Rect rects[10];
        int numRects;
        const float time = 1.5f;
        const float offset = 40.f;
    } name_;
};

bool fmodCheck(const FMOD_RESULT r, const char* const file, const int line)
{
    if(r != FMOD_OK)
    {
        printf("%s:%d FMOD error: %s\n", file, line, FMOD_ErrorString(r));
        return false;
    }
    return true;
}

FMOD_SYSTEM* fmodSystem;

Camera expandToMatchAspectRatio(Camera camera, const vec2 viewportSize)
{
    const vec2 prevSize = camera.size;
    const float viewportAspect = viewportSize.x / viewportSize.y;
    const float cameraAspect = camera.size.x / camera.size.y;

    if(viewportAspect > cameraAspect)
    {
        camera.size.x = camera.size.y * viewportAspect;
    }
    else if(viewportAspect < cameraAspect)
    {
        camera.size.y = camera.size.x / viewportAspect;
    }

    camera.pos.x -= (camera.size.x - prevSize.x) / 2.f;
    camera.pos.y -= (camera.size.y - prevSize.y) / 2.f;
    return camera;
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

    GLFWwindow* window;
    {
        GLFWmonitor* const monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);
        window = glfwCreateWindow(mode->width, mode->height, "cavetiles", monitor, nullptr);
    }

    if(!window)
    {
        glfwTerminate();
        return EXIT_FAILURE;
    }

    // @TODO(matiTechno): fmod error handling? (currently we only print them)
    FCHECK( FMOD_System_Create(&fmodSystem) );
    FCHECK( FMOD_System_Init(fmodSystem, 512, FMOD_INIT_NORMAL, nullptr) );

    glfwMakeContextCurrent(window);
    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
    glfwSwapInterval(1);

    glfwSetKeyCallback(window, keyCallback);
    glfwSetCursorPosCallback(window, cursorPosCallback);
    glfwSetMouseButtonCallback(window, mouseButtonCallback);
    glfwSetScrollCallback(window, scrollCallback);
    glfwSetCharCallback(window, charCallback);

    ImGui::CreateContext();
    ImGui_ImplGlfwGL3_Init(window, false);
    ImGui::StyleColorsDark();

    GLuint program = createProgram(vertexSrc, fragmentSrc);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    Array<WinEvent> events;
    events.reserve(50);
    eventsPtr = &events;

    Scene* scenes[10];
    int numScenes = 1;
    scenes[0] = new LogoScene;

    struct
    {
        float accumulator = 0.f;
        int frameCount = 0;
        float frameTimes[180] = {}; // ms
    } plot;

    double time = glfwGetTime();

    // for now we will handle only the top scene
    while(!glfwWindowShouldClose(window) && numScenes)
    {
        double newTime = glfwGetTime();
        const float dt = newTime - time;
        time = newTime;

        plot.accumulator += dt;
        ++plot.frameCount;

        if(plot.accumulator >= 0.033f)
        {
            memmove(plot.frameTimes, plot.frameTimes + 1, sizeof(plot.frameTimes) -
                                                          sizeof(float));

            plot.frameTimes[getSize(plot.frameTimes) - 1] = plot.accumulator / plot.frameCount
                                                            * 1000.f;
            plot.accumulator = 0.f;
            plot.frameCount = 0;
        }

        FCHECK( FMOD_System_Update(fmodSystem) );

        events.clear();
        glfwPollEvents();
        ImGui_ImplGlfwGL3_NewFrame();

        const bool imguiWantMouse = ImGui::GetIO().WantCaptureMouse;
        const bool imguiWantKeyboard = ImGui::GetIO().WantCaptureKeyboard;

        for(WinEvent& e: events)
        {
            if(imguiWantMouse && ((e.type == WinEvent::MouseButton &&
                                      e.mouseButton.action != GLFW_RELEASE) ||
                                     e.type == WinEvent::Cursor ||
                                     e.type == WinEvent::Scroll))
                e.type = WinEvent::Nil;

            if(imguiWantKeyboard && (e.type == WinEvent::Key))
                e.type = WinEvent::Nil;
        }

        ivec2 fbSize;
        glfwGetFramebufferSize(window, &fbSize.x, &fbSize.y);
        glViewport(0, 0, fbSize.x, fbSize.y);
        glClear(GL_COLOR_BUFFER_BIT);

        Scene& scene = *scenes[numScenes - 1];
        scene.frame_.time = dt;
        scene.frame_.fbSize.x = fbSize.x;
        scene.frame_.fbSize.y = fbSize.y;
        
        ImGui::Begin("cavetiles");
        {
            if(ImGui::Button("quit"))
                glfwSetWindowShouldClose(window, true);

            float max = 0.f;
            float sum = 0.f;

            for(const float t: plot.frameTimes)
            {
                sum += t;
                max = max(max, t);
            }

            const float avg = sum / getSize(plot.frameTimes);

            ImGui::Spacing();
            ImGui::Text("frame time ms");
            ImGui::PushStyleColor(ImGuiCol_Text, {0.f, 0.85f, 0.f, 1.f});
            ImGui::Text("avg   %.3f (%d)", avg, int(1.f / avg * 1000.f + 0.5f));
            ImGui::PushStyleColor(ImGuiCol_Text, {0.9f, 0.f, 0.f, 1.f});
            ImGui::Text("max   %.3f", max);
            ImGui::PopStyleColor(2);
            ImGui::Spacing();
            ImGui::PlotLines("", plot.frameTimes, getSize(plot.frameTimes), 0, nullptr, 0.f,
                             20.f, {0, 60});

            ImGui::Spacing();
            ImGui::Text("vsync");
            ImGui::SameLine();
            if(ImGui::Button("on "))
                glfwSwapInterval(1);
            
            ImGui::SameLine();

            if(ImGui::Button("off"))
                glfwSwapInterval(0);

        }
        ImGui::End();

        scene.processInput(events);
        scene.update();
        scene.render(program);

        ImGui::Render();
        ImGui_ImplGlfwGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);

        Scene* newScene = nullptr;
        newScene = scene.frame_.newScene;
        scene.frame_.newScene = nullptr;

        if(scene.frame_.popMe)
        {
            delete &scene;
            --numScenes;
        }

        if(newScene)
        {
            ++numScenes;
            scenes[numScenes - 1] = newScene;
        }
    }

    for(int i = numScenes - 1; i >= 0; --i)
    {
        delete scenes[i];
    }

    deleteProgram(program);
    ImGui_ImplGlfwGL3_Shutdown();
    ImGui::DestroyContext();

    FCHECK( FMOD_System_Release(fmodSystem) );

    glfwTerminate();
    return EXIT_SUCCESS;
}
