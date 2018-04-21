#pragma once

#include "Array.hpp"
#include "fmod/fmod.h"

#undef max
#define max(a,b) ((a) > (b) ? (a) : (b))

using GLuint = unsigned int;

// use on plain C arrays
template<typename T, int N>
constexpr int getSize(T(&)[N])
{
    return N;
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

// @ better naming?
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
        Nil,
        Key,
        Cursor,
        MouseButton,
        Scroll
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
void updateGLBuffers(GLBuffers& glBuffers, const Rect* rects, int count);
// call bindProgram() first
void renderGLBuffers(GLBuffers& glBuffers, int numRects);
void deleteGLBuffers(GLBuffers& glBuffers);

// returns the number of rects written
int writeTextToBuffer(const Text& text, const Font& font, Rect* buffer, int maxSize);
// bbox
vec2 getTextSize(const Text& text, const Font& font);

bool fmodCheck(FMOD_RESULT r, const char* file, int line); // don't use this

// wrap fmod calls in this
// returns true if function succeeded
#define FCHECK(x) fmodCheck(x, __FILE__, __LINE__)

extern FMOD_SYSTEM* fmodSystem;

struct Camera
{
    vec2 pos;
    vec2 size;
};

Camera expandToMatchAspectRatio(Camera camera, vec2 viewportSize);

class Scene
{
public:
    virtual ~Scene() = default;
    virtual void processInput(const Array<WinEvent>& events) {(void)events;}
    virtual void update() {}
    virtual void render(GLuint program) {(void)program;}

    struct
    {
        // don't modify these
        float time;  // seconds
        vec2 fbSize; // fb = framebuffer

        // @TODO(matiTechno): bool updateWhenNotTop = false;
        bool popMe = false;
        Scene* newScene = nullptr; // assigned ptr must be returned by new
                                   // game loop will call delete on it
    } frame_;
};

struct Anim
{
    float frameDt;
    int numFrames;
    vec4 frames[12];

    float accumulator = 0.f;
    int idx = 0;

    vec4 getCurrentFrame() {return frames[idx];}

    void update(float dt)
    {
        accumulator += dt;
        if(accumulator >= frameDt)
        {
            accumulator -= frameDt;
            ++idx;
            if(idx > numFrames - 1)
                idx = 0;
        }
    }
};

struct Player
{
    vec2 pos;
    vec2 size;
    float vel = 150.f;
    int dynNum = 3;
    void move();
    void dropDynamo();
    vec4 color = {1.f, 0.f, 0.f, 1.f};

    struct
    {
        Anim up;
        Anim down;
        Anim left;
        Anim right;
    } anims;

    Texture texture;
};

class GameScene: public Scene
{
public:
    GameScene();
    ~GameScene() override;
    void processInput(const Array<WinEvent>& events) override;
    void update() override;
    void render(GLuint program) override;

private:
    GLBuffers glBuffers_;
    Player player_;
    Rect rects_[100];

    // Initializing the map, with values indicating the type of a tile
    int tiles_[10][10] =
    {
        1, 1, 2, 2, 3, 1, 3, 2, 1, 1,
        1, 1, 3, 1, 1, 2, 1, 2, 3, 3,
        1, 2, 3, 3, 3, 2, 3, 2, 3, 3,
        2, 1, 1, 2, 2, 2, 1, 1, 3, 2,
        3, 1, 3, 3, 2, 3, 2, 2, 2, 3,
        2, 2, 3, 3, 4, 2, 1, 3, 1, 2,
        3, 2, 2, 3, 1, 3, 3, 3, 1, 1,
        1, 2, 2, 3, 2, 3, 1, 2, 3, 3,
        3, 3, 2, 2, 1, 2, 2, 2, 1, 3,
        1, 2, 3, 3, 3, 2, 3, 3, 1, 1
    };

    struct
    {
        bool L = false;
        bool R = false;
        bool U = false;
        bool D = false;
    } move_;
};
