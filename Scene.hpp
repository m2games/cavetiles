#pragma once

#include "Array.hpp"
#include "fmod/fmod.h"
#include "float.h"

template<typename T>
inline T max(T a, T b) {return a > b ? a : b;}

template<typename T>
inline T min(T a, T b) {return a < b ? a : b;}

using GLuint = unsigned int;

// use on plain C arrays
template<typename T, int N>
constexpr int getSize(T(&)[N])
{
    return N;
}

template<typename T>
struct tvec2
{
    tvec2() = default;
    explicit tvec2(T v): x(v), y(v) {}
    tvec2(T x, T y): x(x), y(y) {}

    template<typename U>
    explicit tvec2(tvec2<U> v): x(v.x), y(v.y) {}

    //                @ const tvec2& ?
    tvec2& operator+=(tvec2 v) {x += v.x; y += v.y; return *this;}
    tvec2& operator+=(T v) {x += v; y += v; return *this;}
    tvec2& operator-=(tvec2 v) {x -= v.x; y -= v.y; return *this;}
    tvec2& operator-=(T v) {x -= v; y -= v; return *this;}
    tvec2& operator*=(tvec2 v) {x *= v.x; y *= v.y; return *this;}
    tvec2& operator*=(T v) {x *= v; y *= v; return *this;}
    tvec2& operator/=(tvec2 v) {x /= v.x; y /= v.y; return *this;}
    tvec2& operator/=(T v) {x /= v; y /= v; return *this;}

    tvec2 operator+(tvec2 v) const {return {x + v.x, y + v.y};}
    tvec2 operator+(T v)     const {return {x + v, y + v};}
    tvec2 operator-(tvec2 v) const {return {x - v.x, y - v.y};}
    tvec2 operator-(T v)     const {return {x - v, y - v};}
    tvec2 operator*(tvec2 v) const {return {x * v.x, y * v.y};}
    tvec2 operator*(T v)     const {return {x * v, y * v};}
    tvec2 operator/(tvec2 v) const {return {x / v.x, y / v.y};}
    tvec2 operator/(T v)     const {return {x / v, y / v};}

    bool operator==(tvec2 v) const {return x == v.x && y == v.y;}
    bool operator!=(tvec2 v) const {return !(*this == v);}

    T x;
    T y;
};

template<typename T>
inline tvec2<T> operator*(T scalar, tvec2<T> v) {return v * scalar;}

using ivec2 = tvec2<int>;
using vec2  = tvec2<float>;

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

// GAME STRUCTURES

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
    vec4 getCurrentFrame() const {return frames[idx];}
    void update(float dt);

    float frameDt;
    int numFrames;
    vec4 frames[12];
    float accumulator = 0.f;
    int idx = 0;
    bool ended = false;
};

struct Explosion
{
    Anim anim;
    ivec2 tile;
    float size;
    vec4 color = {1.f, 1.f, 1.f, 1.f};
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

struct Player
{
    vec2 pos;
    float vel;
    int dir = Dir::Nil;
    float dropCooldown;
    int hp;
    int score = 0;
    char name[20] = {};

    // only for the visuals; kept in simulation for convenience
    float dmgTimer = 0.f;
    int prevDir;
};

struct PlayerView
{
    Texture* texture;
    Anim anims[Dir::Count];
};

struct Bomb
{
    void addPlayer(int idx);
    void removePlayer(int idx);
    bool findPlayer(int idx) const;

    ivec2 tile;
    int range = 2;
    float timer;

    // players allowed to stand on a bomb (used for resolving collisions)
    int playerIdxs[2] = {-1, -1};
};

struct ExploEvent
{
    enum Type
    {
        Crate,
        OtherBomb,
        EmptyTile,
        Player,
        Wall
    };

    ivec2 tile;
    int type;
};

struct Action // @TODO: rename to PlayerAction?
{
    bool up = false;
    bool down = false;
    bool left = false;
    bool right = false;
    bool drop = false;
};

struct Simulation
{
    Simulation();
    void setNewGame();
    void processPlayerInput(const Action& action, const char* name);
    void update(float dt, FixedArray<ExploEvent, 50>& exploEvents); // in seconds

    enum {MapSize = 13, HP = 3};
    static const float dropCooldown_;
    static const float tileSize_;
    static const vec2 dirVecs_[Dir::Count];

    // this must be serializable !!! (memcpy for now (on the client side); server sends it as
    // readable text)

    int tiles_[MapSize][MapSize] = {}; // initialized to 0
    Player players_[2];
    FixedArray<Bomb, 50> bombs_;
    float timeToStart_ = 0.f;
};

#define SIM_STATIC_DEF \
    const float Simulation::dropCooldown_ = 1.f; \
    const float Simulation::tileSize_ = 20.f; \
    const vec2  Simulation::dirVecs_[Dir::Count] = {{0.f, 0.f}, {0.f, -1.f}, {0.f, 1.f}, \
        {-1.f, 0.f}, {1.f, 0.f}};

namespace netcode
{

struct Cmd
{
    enum
    {
        _nil,

        Ping,
        Pong,
        Chat,
        GameFull,

        // name cmds have the name as the payload
        SetName,
        NameOk,
        MustRename,

        _count
    };
};

struct Client
{
    Client();
    ~Client();
    Client(const Client&) = delete;
    Client(Client&&) = delete;
    Client& operator=(const Client&) = delete;
    Client& operator=(Client&&) = delete;

    // dt is seconds
    void update(float dt, const char* name,
                FixedArray<ExploEvent, 50>& eevents, Action& playerAction);

    Array<char> sendBuf, recvBuf, logBuf;
    int recvBufNumUsed = 0;
    const int maxNameSize = 19;
    const float timerAliveMax = 5.f;
    const float timerReconnectMax = 5.f;
    bool hasToReconnect = true; // due to tcp error or no server response
    int sockfd = -1;
    float timerReconnect = timerReconnectMax;
    bool simReadyToSync = false;
    bool inGame = false;
    char inGameName[20]; // this will be used to identify the player in Simulation
    bool sendSetNameMsg = false;
    float timerSendSetNameMsg = timerReconnectMax;

    // initialized in connect() (see cpp file)
    bool serverAlive;
    float timerAlive;

    Simulation sim;
};

// use this to e.g. send a chat message
void addMsg(Array<char>& sendBuf, int cmd, const char* payload = "");

} // netcode


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
    Rect rects_[Simulation::MapSize * Simulation::MapSize];
    FixedArray<Explosion, 50> explosions_;
    Emitter emitter_;
    Font font_;
    bool showScore_ = true;

    struct
    {
        Texture tile;
        Texture player1;
        Texture player2;
        Texture bomb;
        Texture explosion;
    } textures_;

    struct
    {
        FMOD_SOUND* bomb;
        FMOD_SOUND* crateExplosion;
    } sounds_;

    netcode::Client netClient_;
    char nameToSetBuf_[20] = "player1";
    char inputNameBuf_[20] = {}; // flush to nameToSetBuf_ on ENTER
    char chatBuf_[128] = {};
    Simulation sim_;
    PlayerView playerViews_[2];
    Action actions_[2];
    FixedArray<ExploEvent, 50> exploEvents_;
};
