#pragma once

#include "Array.hpp"
#include "fmod/fmod.h"
#include "float.h"

#undef max
#define max(a,b) ((a) > (b) ? (a) : (b))
#undef min
#define min(a,b) ((a) < (b) ? (a) : (b))

using GLuint = unsigned int;

// use on plain C arrays
template<typename T, int N>
constexpr int getSize(T(&)[N])
{
    return N;
}

// @TODO(matiTechno): add some operators / functions for vectors and refactor existing code

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
// @TODO(matiTechno): we might want updateSubGLBuffers()
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

// [min, max]
float getRandomFloat(float min, float max);
int getRandomInt(int min, int max);

class Scene
{
public:
    virtual ~Scene() = default;
    virtual void processInput(const Array<WinEvent>& events) {(void)events;}
    virtual void update() {}
    virtual void render(GLuint program) {(void)program;}

    struct
    {
        // these are set in the main loop before processInput() call
        float time;  // seconds
        vec2 fbSize; // fb = framebuffer

        // @TODO(matiTechno): bool updateWhenNotTop = false;
        bool popMe = false;
        Scene* newScene = nullptr; // assigned ptr must be returned by new
                                   // game loop will call delete on it
    } frame_;
};

template<typename T>
struct Range
{
    T min, max;
};

struct Particle
{
    float life;
    vec2 vel;
};

struct Emitter
{
    // call after setting all the emitter parameters
    void reserve();

    void update(float dt);

    struct
    {
        vec2 pos;
        vec2 size;
        float hz;
        float activeTime = FLT_MAX;
    } spawn;

    struct
    {
        Range<float> life;
        Range<float> size;
        Range<vec2> vel;
        Range<vec4> color;
    } particleRanges;

    // end of parameters
    float accumulator = 0.f;
    int numActive = 0;
    Array<Particle> particles;
    Array<Rect> rects;
};

struct Anim
{
    float frameDt;
    int numFrames;
    vec4 frames[12];
    float accumulator = 0.f;
    int idx = 0;

    vec4 getCurrentFrame() const {return frames[idx];}
    void update(float dt);
};

struct Dir
{
    enum
    {
        Nil,
        Up,
        Down,
        Left,
        Right,
        Count
    };
};

struct Dynamite
{
    ivec2 tile;
    int range = 2;
    Texture* texture;
    float timer;
};

struct Player
{
    vec2 pos;
    float vel;
    int dir = Dir::Nil;
    Texture* texture;
    Anim anims[Dir::Count];
    bool isOnDynamite = false;
    // for animation only
    int prevDir;
    float dropCooldown = 0.f;
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
    enum {MapSize = 15, MaxDynamites = 50};
    const float tileSize_ = 20.f;

    GLBuffers glBuffers_;
    Rect rects_[MapSize * MapSize];
    int tiles_[MapSize][MapSize] = {}; // initialized to 0
    Player players_[2];
    Dynamite dynamites_[MaxDynamites];
    int numDynamites_ = 0;
    vec2 dirVecs_[Dir::Count] = {{0.f, 0.f}, {0.f, -1.f}, {0.f, 1.f}, {-1.f, 0.f}, {1.f, 0.f}};
    Emitter emitter_;
    Texture tileTexture_;
    Texture player1Texture_;
    Texture player2Texture_;
    Texture dynamiteTexture_;

    struct
    {
        bool up = false;
        bool down = false;
        bool left = false;
        bool right = false;
        bool drop = false;
    } keys_[2];
};
