#pragma once

class Sprite
{
public:
    Sprite();
    Sprite(const char* const img_path);
    Sprite(const char* const img_path, float xPos, float yPos);
    Sprite(const char* const img_path, float xPos, float yPos, bool isMovable);

    void update();
    void render();

    void move(float x, float y);
    void destroy();                         // Probably those should be
    void isCollision(Sprite sprite);        // virtual, eh?
    void setPosition()

private:
    float xPos;
    float yPos;
    bool isMovable;
    bool isAlive;
    //Texture texture;
};

class Player: public Sprite
{
public:
    void dropBomb();

private:
    int num_of_bombs;
}
