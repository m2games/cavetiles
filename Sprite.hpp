#pragma once

struct vec2
{
    float x;
    float y;
};

class Sprite
{
public:
    Sprite();
    Sprite(const char* img_path, vec2 v, bool _isMovable);

    void render();
    void move(vec2 v);

    void destroy();
    bool isCollision(Sprite sprite);
    void setPosition(vec2 v);
    void setScale(vec2 v);

private:

    vec2 position;
    vec2 scale;
    vec2 size;
    bool isMovable;
    // Refers whether the object should exist or not.
    bool isAlive;
    //Texture texture;
};

// Player class extends Sprite class by adding the bombs.
class Player: public Sprite
{
public:
    void dropBomb();

private:
    int num_of_bombs;
};
