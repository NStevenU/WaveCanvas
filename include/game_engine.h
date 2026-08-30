#pragma once

#include <Arduino.h>
#include <U8g2lib.h>
#include "encoder_input.h"

enum GameType {
    GAME_NONE,
    GAME_PIANO,
    GAME_PONG,
    GAME_BLOCK_STACK,
    GAME_BRICK,
    GAME_SNAKE
};

class GameEngine {
public:
    static void init(GameType type);
    static void update(U8G2& u8g2);
    static void handleEncoderEvent(EncoderEvent event);
    static void exitGame();
    static bool isGameActive();
    static GameType getCurrentGame();

private:
    static GameType activeGame;
    static unsigned long lastFrameTime;
    static bool isSound8Bit;

    // --- 1. 가상 피아노 (Virtual Piano) ---
    static int pianoKeyIndex; // 0 ~ 13 (C4 ~ B5)
    static uint8_t pianoLastNote;
    static unsigned long pianoNoteOffTime;
    static void initPiano();
    static void updatePiano(U8G2& u8g2);
    static void handlePianoInput(EncoderEvent event);

    // --- 2. 핑퐁 (1972 Atari Pong) ---
    static float pongBallX, pongBallY;
    static float pongBallVX, pongBallVY;
    static int pongPlayerY;
    static int pongAiY;
    static int pongPlayerScore;
    static int pongAiScore;
    static int pongRound;
    static int pongPaddleH;
    static bool pongGameOver;
    static void initPong();
    static void updatePong(U8G2& u8g2);
    static void handlePongInput(EncoderEvent event);
    static void resetPongBall(bool toPlayer);

    // --- 3. 블록 쌓기 (8-Bit Block Stack) ---
    static uint8_t board[20][10];
    static int currentPiece;
    static int pieceRotation;
    static int pieceX, pieceY;
    static int nextPiece;
    static unsigned long lastDropTime;
    static unsigned long dropInterval;
    static int stackScore;
    static int stackLines;
    static int stackLevel;
    static int stackLevelLines;
    static bool stackGameOver;
    static unsigned long bgmStepTime;
    static int bgmStepIndex;
    static void initBlockStack();
    static void updateBlockStack(U8G2& u8g2);
    static void handleBlockStackInput(EncoderEvent event);
    static bool checkCollision(int px, int py, int piece, int rot);
    static void lockPiece();
    static void clearLines();
    static void spawnPiece();
    static void stepKorobeinikiBGM();

    // --- 4. 벽돌깨기 (1976 Brick Breaker) ---
    static float brickBallX, brickBallY;
    static float brickBallVX, brickBallVY;
    static int brickPaddleX;
    static int brickPaddleW;
    static uint8_t bricks[4][8]; // 4줄 x 8개 = 32개 벽돌 (내구도 0, 1, 2, 3)
    static int brickScore;
    static int brickLives;
    static int brickWave;
    static bool brickBallStuck;
    static bool brickGameOver;
    static void initBrick();
    static void updateBrick(U8G2& u8g2);
    static void handleBrickInput(EncoderEvent event);
    static void resetBrickLevel();

    // --- 5. 스네이크 (Snake) ---
    static struct Point { int8_t x, y; } snakeBody[64];
    static int snakeLen;
    static int8_t snakeDirX, snakeDirY;
    static Point snakeFood;
    static unsigned long lastSnakeMoveTime;
    static unsigned long snakeMoveInterval;
    static int snakeScore;
    static int snakeLives;
    static int snakeStage;
    static int snakeStageFoodCount;
    static Point snakeObstacles[16];
    static int snakeObstacleCount;
    static bool snakeGameOver;
    static void initSnake();
    static void updateSnake(U8G2& u8g2);
    static void handleSnakeInput(EncoderEvent event);
    static void spawnSnakeFood();
    static void generateObstacles(int stage);
    static void resetSnakeRound();

    // --- 공통 8비트 아케이드 사운드 합성기 (논블로킹 타이머 기반) ---
    struct SFXSlot {
        uint8_t ch;
        uint8_t note;
        unsigned long offTime;
        bool active;
    };
    static SFXSlot sfxSlots[4];
    static void play8BitSound(uint8_t note, uint16_t durationMs, uint8_t program = 80, uint8_t ch = 15, uint8_t vel = 90);
};
