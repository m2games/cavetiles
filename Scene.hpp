#pragma once

#include "Array.hpp"

#undef max
#define max(a,b) ((a) > (b) ? (a) : (b))

using GLuint = unsigned int;

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

struct FragmentMode
{
    enum
    {
        Color = 0,
        Texture = 1,
        Font = 2
    };
};

struct Texture
{
    ivec2 size;
    GLuint id;
};

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

struct Camera
{
    vec2 pos;
    vec2 size;
};

struct GLBuffers
{
    GLuint vao;
    GLuint vbo;
    GLuint rectBo;
};

struct WinEvent
{
    enum Type
    {
        Key,
        Cursor,
        MouseButton,
        Scroll,
        Nil
    };

    Type type;

    // glfw values
    union
    {
        struct
        {
            int key;
            int action;
            int mods;
        } key;

        struct
        {
            vec2 pos;
        } cursor;

        struct
        {
            int button;
            int action;
            int mods;
        } mouseButton;

        struct
        {
            vec2 offset;
        } scroll;
    };
};

// call bindeProgram() first
void uniform1i(GLuint program, const char* name, int i);
void uniform1f(GLuint program, const char* name, float f);
void uniform2f(GLuint program, const char* name, float f1, float f2);
void uniform2f(GLuint program, const char* name, vec2 v);
void uniform3f(GLuint program, const char* name, float f1, float f2, float f3);
void uniform4f(GLuint program, const char* name, float f1, float f2, float f3, float f4);
void uniform4f(GLuint program, const char* name, vec4 v);

void bindTexture(const Texture& texture, GLuint unit = 0);
// delete with deleteTexture()
Texture createTextureFromFile(const char* filename);
void deleteTexture(const Texture& texture);

// delete with deleteFont()
Font createFontFromFile(const char* filename, int fontSize, int textureWidth);
void deleteFont(Font& font);

// returns 0 on failure
// program must be deleted with deleteProgram() (if != 0)
GLuint createProgram(const char* vertexSrc, const char* fragmentSrc);
void deleteProgram(GLuint program);
void bindProgram(const GLuint program);
 
// delete with deleteGLBuffers()
GLBuffers createGLBuffers();
void fillGLRectBuffer(GLuint rectBo, const Rect* rects, int count);
// call bindProgram() first
void renderGLBuffer(GLuint vao, int numRects);
void deleteGLBuffers(GLBuffers& glBuffers);

// returns the number of rects written
int writeTextToBuffer(const Text& text, const Font& font, Rect* buffer, int maxSize);
// bbox
vec2 getTextSize(const Text& text, const Font& font);

class Scene
{
public:
    virtual void init() {}
    virtual void shutdown() {}
    virtual void processInput(const Array<WinEvent>& events) {(void)events;}
    virtual void update() {}
    virtual void render(GLuint program) {(void)program;}

    struct
    {
        float time;
        float framebufferSize;
        bool popMe = false;
        Scene* newScene = nullptr;
    } frame_;
};
