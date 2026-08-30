#include "game_engine.h"
#include "audio_engine.h"
#include "midi_sequencer.h"
#include "tetris_midi_data.h"
#include "u8g2_font_galmuri9.h"
#include <math.h>

GameType GameEngine::activeGame = GAME_NONE;
unsigned long GameEngine::lastFrameTime = 0;
bool GameEngine::isSound8Bit = true;

// --- 1. 가상 피아노 상태 ---
int GameEngine::pianoKeyIndex = 0;
uint8_t GameEngine::pianoLastNote = 0;
unsigned long GameEngine::pianoNoteOffTime = 0;

static const uint8_t PIANO_NOTES[14] = {
    60, 62, 64, 65, 67, 69, 71, // C4, D4, E4, F4, G4, A4, B4 (흰건반 7개)
    72, 74, 76, 77, 79, 81, 83  // C5, D5, E5, F5, G5, A5, B5 (흰건반 7개)
};
static const char* PIANO_NAMES[14] = {
    "C4", "D4", "E4", "F4", "G4", "A4", "B4",
    "C5", "D5", "E5", "F5", "G5", "A5", "B5"
};

// --- 2. 핑퐁 상태 ---
float GameEngine::pongBallX = 64;
float GameEngine::pongBallY = 38;
float GameEngine::pongBallVX = 1.6f;
float GameEngine::pongBallVY = 1.0f;
int GameEngine::pongPlayerY = 28;
int GameEngine::pongAiY = 28;
int GameEngine::pongPlayerScore = 0;
int GameEngine::pongAiScore = 0;
int GameEngine::pongRound = 1;
int GameEngine::pongPaddleH = 12;
bool GameEngine::pongGameOver = false;

// --- 3. 블록 쌓기 상태 ---
uint8_t GameEngine::board[20][10];
int GameEngine::currentPiece = 0;
int GameEngine::pieceRotation = 0;
int GameEngine::pieceX = 3;
int GameEngine::pieceY = 0;
int GameEngine::nextPiece = 0;
unsigned long GameEngine::lastDropTime = 0;
unsigned long GameEngine::dropInterval = 500;
int GameEngine::stackScore = 0;
int GameEngine::stackLines = 0;
int GameEngine::stackLevel = 1;
int GameEngine::stackLevelLines = 0;
bool GameEngine::stackGameOver = false;
unsigned long GameEngine::bgmStepTime = 0;
int GameEngine::bgmStepIndex = 0;

// 7가지 테트로미노 정의 (4x4 비트맵 표현)
static const uint16_t TETROMINOES[7][4] = {
    { 0x0F00, 0x2222, 0x00F0, 0x4444 }, // I
    { 0x8E00, 0x6440, 0x0E20, 0x44C0 }, // J
    { 0x2E00, 0x4460, 0x0E80, 0xC440 }, // L
    { 0x6600, 0x6600, 0x6600, 0x6600 }, // O
    { 0x6C00, 0x4620, 0x06C0, 0x8C40 }, // S
    { 0x4E00, 0x4640, 0x0E40, 0x4C40 }, // T
    { 0xC600, 0x2640, 0x0C60, 0x4C80 }  // Z
};

// --- 4. 벽돌깨기 상태 ---
float GameEngine::brickBallX = 64;
float GameEngine::brickBallY = 50;
float GameEngine::brickBallVX = 1.4f;
float GameEngine::brickBallVY = -1.4f;
int GameEngine::brickPaddleX = 54;
int GameEngine::brickPaddleW = 18;
uint8_t GameEngine::bricks[4][8]; // 내구도 0, 1, 2, 3
int GameEngine::brickScore = 0;
int GameEngine::brickLives = 5;
int GameEngine::brickWave = 1;
bool GameEngine::brickBallStuck = true;
bool GameEngine::brickGameOver = false;

// --- 5. 스네이크 상태 ---
GameEngine::Point GameEngine::snakeBody[64];
int GameEngine::snakeLen = 4;
int8_t GameEngine::snakeDirX = 1;
int8_t GameEngine::snakeDirY = 0;
GameEngine::Point GameEngine::snakeFood = { 14, 5 };
unsigned long GameEngine::lastSnakeMoveTime = 0;
unsigned long GameEngine::snakeMoveInterval = 130;
int GameEngine::snakeScore = 0;
int GameEngine::snakeLives = 5;
int GameEngine::snakeStage = 1;
int GameEngine::snakeStageFoodCount = 0;
GameEngine::Point GameEngine::snakeObstacles[16];
int GameEngine::snakeObstacleCount = 0;
bool GameEngine::snakeGameOver = false;

GameEngine::SFXSlot GameEngine::sfxSlots[4] = {
    {0, 0, 0, false},
    {0, 0, 0, false},
    {0, 0, 0, false},
    {0, 0, 0, false}
};

// --- 8비트 사운드 발음 헬퍼 (논블로킹 타이머 슬롯 기반 - FreeRTOS 태스크 오버헤드 0) ---
void GameEngine::play8BitSound(uint8_t note, uint16_t durationMs, uint8_t program, uint8_t ch, uint8_t vel) {
    // 🌟 효과음 채널(Ch 15)을 확실하게 8비트 아케이드 사운드(Square Lead, Program 80) 및 Bank 0으로 고정
    AudioEngine::setBank(ch, 0);
    AudioEngine::programChange(ch, program);
    AudioEngine::controlChange(ch, 7, 95); // 효과음 음량 확보
    AudioEngine::noteOn(ch, note, vel);
    
    unsigned long now = millis();
    // 빈 슬롯 또는 가장 오래된 슬롯 찾기
    int targetSlot = -1;
    for (int i = 0; i < 4; i++) {
        if (!sfxSlots[i].active) {
            targetSlot = i;
            break;
        }
    }
    if (targetSlot == -1) {
        targetSlot = 0;
        AudioEngine::noteOff(sfxSlots[0].ch, sfxSlots[0].note);
    }
    sfxSlots[targetSlot].ch = ch;
    sfxSlots[targetSlot].note = note;
    sfxSlots[targetSlot].offTime = now + durationMs;
    sfxSlots[targetSlot].active = true;
}

// ==========================================
// 공통 인터페이스
// ==========================================

void GameEngine::init(GameType type) {
    activeGame = type;
    lastFrameTime = millis();

    for (int i = 0; i < 4; i++) {
        sfxSlots[i].active = false;
    }

    MIDISequencer::stop();
    MIDISequencer::setLoop(false);
    AudioEngine::panic();

    // Ch 15 (16번 채널) 8비트 아케이드 SFX 전용 악기(Square Lead) 사전 등록
    AudioEngine::programChange(15, 80);
    AudioEngine::controlChange(15, 7, 95);
    AudioEngine::controlChange(15, 10, 64);

    switch (type) {
        case GAME_PIANO:       initPiano(); break;
        case GAME_PONG:        initPong(); break;
        case GAME_BLOCK_STACK: initBlockStack(); break;
        case GAME_BRICK:       initBrick(); break;
        case GAME_SNAKE:       initSnake(); break;
        default: break;
    }
}

void GameEngine::exitGame() {
    activeGame = GAME_NONE;
    for (int i = 0; i < 4; i++) {
        if (sfxSlots[i].active) {
            AudioEngine::noteOff(sfxSlots[i].ch, sfxSlots[i].note);
            sfxSlots[i].active = false;
        }
    }
    MIDISequencer::stop();
    MIDISequencer::setLoop(false);
    AudioEngine::panic();
}

bool GameEngine::isGameActive() {
    return (activeGame != GAME_NONE);
}

GameType GameEngine::getCurrentGame() {
    return activeGame;
}

void GameEngine::update(U8G2& u8g2) {
    if (activeGame == GAME_NONE) return;

    unsigned long now = millis();

    if (pianoNoteOffTime > 0 && now >= pianoNoteOffTime) {
        if (pianoLastNote > 0) {
            AudioEngine::noteOff(0, pianoLastNote);
            pianoLastNote = 0;
        }
        pianoNoteOffTime = 0;
    }

    for (int i = 0; i < 4; i++) {
        if (sfxSlots[i].active && now >= sfxSlots[i].offTime) {
            AudioEngine::noteOff(sfxSlots[i].ch, sfxSlots[i].note);
            sfxSlots[i].active = false;
        }
    }

    switch (activeGame) {
        case GAME_PIANO:       updatePiano(u8g2); break;
        case GAME_PONG:        updatePong(u8g2); break;
        case GAME_BLOCK_STACK: updateBlockStack(u8g2); break;
        case GAME_BRICK:       updateBrick(u8g2); break;
        case GAME_SNAKE:       updateSnake(u8g2); break;
        default: break;
    }
}

void GameEngine::handleEncoderEvent(EncoderEvent event) {
    if (activeGame == GAME_NONE) return;

    switch (activeGame) {
        case GAME_PIANO:       handlePianoInput(event); break;
        case GAME_PONG:        handlePongInput(event); break;
        case GAME_BLOCK_STACK: handleBlockStackInput(event); break;
        case GAME_BRICK:       handleBrickInput(event); break;
        case GAME_SNAKE:       handleSnakeInput(event); break;
        default: break;
    }
}

// ==========================================
// 1. 가상 피아노 (Virtual Piano)
// ==========================================

void GameEngine::initPiano() {
    pianoKeyIndex = 0;
    pianoLastNote = 0;
    pianoNoteOffTime = 0;
    isSound8Bit = false;
    AudioEngine::programChange(0, 0);
}

void GameEngine::updatePiano(U8G2& u8g2) {
    u8g2.setFont(u8g2_font_galmuri9);
    
    char hdr[32];
    snprintf(hdr, sizeof(hdr), "[PIANO] %s (%s)", PIANO_NAMES[pianoKeyIndex], isSound8Bit ? "8-Bit" : "Grand");
    u8g2.drawUTF8(0, 9, hdr);
    u8g2.drawHLine(0, 14, 128);

    int startX = 1;
    int whiteW = 9;
    int whiteH = 34;
    int blackW = 5;
    int blackH = 19;
    int topY = 16;

    for (int i = 0; i < 14; i++) {
        int kx = startX + (i * whiteW);
        if (i == pianoKeyIndex) {
            u8g2.drawBox(kx, topY, whiteW - 1, whiteH);
        } else {
            u8g2.drawFrame(kx, topY, whiteW - 1, whiteH);
        }
    }

    static const int blackOffsets[10] = { 0, 1, 3, 4, 5, 7, 8, 10, 11, 12 };
    for (int i = 0; i < 10; i++) {
        int bo = blackOffsets[i];
        int bx = startX + (bo * whiteW) + (whiteW - (blackW / 2) - 1);
        u8g2.setDrawColor(0);
        u8g2.drawBox(bx, topY, blackW, blackH);
        u8g2.setDrawColor(1);
        u8g2.drawFrame(bx, topY, blackW, blackH);
    }

    u8g2.setFont(u8g2_font_4x6_tf);
    u8g2.drawStr(2, 62, "Rotate:Key  Click:Play  [5s Exit]");
}

void GameEngine::handlePianoInput(EncoderEvent event) {
    if (event == ENC_ROTATE_CW) {
        if (pianoKeyIndex < 13) pianoKeyIndex++;
    } else if (event == ENC_ROTATE_CCW) {
        if (pianoKeyIndex > 0) pianoKeyIndex--;
    } else if (event == ENC_BUTTON_CLICK) {
        if (pianoLastNote > 0) {
            AudioEngine::noteOff(0, pianoLastNote);
            pianoLastNote = 0;
        }
        for (int i = 0; i < 14; i++) {
            AudioEngine::noteOff(0, PIANO_NOTES[i]);
        }
        uint8_t note = PIANO_NOTES[pianoKeyIndex];
        AudioEngine::programChange(0, isSound8Bit ? 80 : 0);
        AudioEngine::noteOn(0, note, 100);
        pianoLastNote = note;
        pianoNoteOffTime = millis() + 450;
    } else if (event == ENC_BUTTON_LONG) {
        if (pianoLastNote > 0) {
            AudioEngine::noteOff(0, pianoLastNote);
            pianoLastNote = 0;
        }
        for (int i = 0; i < 14; i++) {
            AudioEngine::noteOff(0, PIANO_NOTES[i]);
        }
        isSound8Bit = !isSound8Bit;
        AudioEngine::programChange(0, isSound8Bit ? 80 : 0);
    }
}

// ==========================================
// 2. 핑퐁 (1972 Atari Pong - 무한 라운드 5점 서바이벌)
// ==========================================

void GameEngine::initPong() {
    pongRound = 1;
    pongPaddleH = 12;
    pongPlayerScore = 0;
    pongAiScore = 0;
    pongPlayerY = 28;
    pongAiY = 28;
    pongGameOver = false;
    resetPongBall(true);
}

void GameEngine::resetPongBall(bool toPlayer) {
    pongBallX = 64.0f;
    pongBallY = 38.0f;
    float spd = 1.5f + (float)(pongRound - 1) * 0.15f;
    if (spd > 3.0f) spd = 3.0f;
    pongBallVX = toPlayer ? -spd : spd;
    pongBallVY = (((rand() % 100) / 50.0f) - 1.0f) * (spd * 0.7f);
}

void GameEngine::updatePong(U8G2& u8g2) {
    u8g2.setFont(u8g2_font_galmuri9);

    // 상단 헤더 (Yellow 영역 Y: 0 ~ 14)
    char scoreStr[32];
    snprintf(scoreStr, sizeof(scoreStr), "R:%d Y:%d A:%d", pongRound, pongPlayerScore, pongAiScore);
    u8g2.drawUTF8(2, 9, scoreStr);
    u8g2.setFont(u8g2_font_4x6_tf);
    u8g2.drawStr(90, 8, "[5s Exit]");
    u8g2.drawHLine(0, 14, 128);

    // 게임 플레이 필드 (100% Blue 영역 Y: 17 ~ 63)
    if (!pongGameOver) {
        pongBallX += pongBallVX;
        pongBallY += pongBallVY;

        // 상하 벽 충돌
        if (pongBallY <= 17.0f) {
            pongBallY = 17.0f;
            pongBallVY = fabsf(pongBallVY);
            play8BitSound(57, 30, 80); // Boop
        } else if (pongBallY >= 61.0f) {
            pongBallY = 61.0f;
            pongBallVY = -fabsf(pongBallVY);
            play8BitSound(57, 30, 80); // Boop
        }

        // AI 패들 추적 (라운드가 높을수록 더 빠르고 정밀하게 추적)
        float aiTarget = pongBallY - 5;
        int aiSpeed = (pongRound >= 4) ? 2 : 1;
        if (pongAiY < aiTarget) pongAiY += aiSpeed;
        else if (pongAiY > aiTarget) pongAiY -= aiSpeed;
        if (pongAiY < 17) pongAiY = 17;
        if (pongAiY > 51) pongAiY = 51;

        // 플레이어 패들 충돌 검사 (동적 패들 높이 pongPaddleH)
        if (pongBallX <= 5.0f && pongBallX >= 2.0f) {
            if (pongBallY >= pongPlayerY - 2 && pongBallY <= pongPlayerY + pongPaddleH + 2) {
                pongBallVX = fabsf(pongBallVX) * 1.05f;
                float maxSpd = 3.2f;
                if (pongBallVX > maxSpd) pongBallVX = maxSpd;
                float halfH = (float)pongPaddleH / 2.0f;
                float hitOffset = (pongBallY - (pongPlayerY + halfH)) / halfH;
                pongBallVY = hitOffset * 1.9f;
                play8BitSound(69, 35, 80); // Pong
            }
        }

        // AI 패들 충돌 검사
        if (pongBallX >= 121.0f && pongBallX <= 124.0f) {
            if (pongBallY >= pongAiY - 2 && pongBallY <= pongAiY + 14) {
                pongBallVX = -fabsf(pongBallVX) * 1.05f;
                float hitOffset = (pongBallY - (pongAiY + 6)) / 6.0f;
                pongBallVY = hitOffset * 1.8f;
                play8BitSound(69, 35, 80); // Pong
            }
        }

        // 5점 승부 판정
        if (pongBallX < 0) {
            pongAiScore++;
            play8BitSound(48, 200, 80);
            if (pongAiScore >= 5) {
                // 패배 -> 무한 모드 게임오버!
                pongGameOver = true;
            } else {
                resetPongBall(false);
            }
        } else if (pongBallX > 128) {
            pongPlayerScore++;
            play8BitSound(76, 200, 80);
            if (pongPlayerScore >= 5) {
                // 승리 -> 다음 라운드 승급! (무한 루프)
                pongRound++;
                pongPlayerScore = 0;
                pongAiScore = 0;
                pongPaddleH = 12 - (pongRound - 1) / 2;
                if (pongPaddleH < 6) pongPaddleH = 6;
                play8BitSound(84, 300, 80); // 승급 팡파레!
                resetPongBall(true);
            } else {
                resetPongBall(true);
            }
        }
    }

    // 중앙 점선 네트 (Y: 17 ~ 63)
    for (int y = 17; y < 64; y += 4) {
        u8g2.drawVLine(64, y, 2);
    }

    // 플레이어 패들 (좌측: x=2, w=2, h=pongPaddleH)
    u8g2.drawBox(2, pongPlayerY, 2, pongPaddleH);

    // AI 패들 (우측: x=124, w=2, h=12)
    u8g2.drawBox(124, pongAiY, 2, 12);

    // 공 (2x2)
    u8g2.drawBox((int)pongBallX, (int)pongBallY, 2, 2);

    if (pongGameOver) {
        u8g2.setFont(u8g2_font_galmuri9);
        u8g2.setDrawColor(0);
        u8g2.drawBox(10, 22, 108, 28);
        u8g2.setDrawColor(1);
        u8g2.drawFrame(10, 22, 108, 28);
        u8g2.drawUTF8(14, 35, "GAME OVER (Click)");
        char finalStr[32];
        snprintf(finalStr, sizeof(finalStr), "FINAL: ROUND %02d", pongRound);
        u8g2.drawUTF8(14, 47, finalStr);
    }
}

void GameEngine::handlePongInput(EncoderEvent event) {
    if (pongGameOver) {
        if (event == ENC_BUTTON_CLICK) {
            initPong();
        }
        return;
    }

    int maxY = 64 - pongPaddleH - 1;
    if (event == ENC_ROTATE_CW) {
        pongPlayerY += 4;
        if (pongPlayerY > maxY) pongPlayerY = maxY;
    } else if (event == ENC_ROTATE_CCW) {
        pongPlayerY -= 4;
        if (pongPlayerY < 17) pongPlayerY = 17;
    }
}

// ==========================================
// 3. 블록 쌓기 (8-Bit Block Stack - 10줄 무한 레벨업)
// ==========================================

void GameEngine::initBlockStack() {
    memset(board, 0, sizeof(board));
    stackScore = 0;
    stackLines = 0;
    stackLevel = 1;
    stackLevelLines = 0;
    stackGameOver = false;
    dropInterval = 500;
    lastDropTime = millis();
    nextPiece = rand() % 7;

    // Tetris - Korobeiniki.mid 풀 멀티트랙 MIDI 무한 루프 재생 시작!
    MIDISequencer::loadMemory(TETRIS_KOROBEINIKI_MIDI, TETRIS_KOROBEINIKI_MIDI_SIZE, "Korobeiniki");
    MIDISequencer::setLoop(true);
    MIDISequencer::play();

    SemaphoreHandle_t mutex = AudioEngine::getMutex();
    if (mutex && xSemaphoreTake(mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        uint8_t tetrisChannels[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
        for (uint8_t ch : tetrisChannels) {
            AudioEngine::controlChangeDirect(ch, 7, 90);
            AudioEngine::controlChangeDirect(ch, 11, 127);
        }
        xSemaphoreGive(mutex);
    }

    spawnPiece();
}

void GameEngine::spawnPiece() {
    currentPiece = nextPiece;
    nextPiece = rand() % 7;
    pieceRotation = 0;
    pieceX = 3;
    pieceY = 8; // 가시 영역(9~19행) 상단 바로 안쪽에 즉시 스폰!

    if (checkCollision(pieceX, pieceY, currentPiece, pieceRotation)) {
        stackGameOver = true;
        MIDISequencer::stop();
        MIDISequencer::setLoop(false);
        AudioEngine::panic();
        play8BitSound(44, 400, 80);
    }
}

bool GameEngine::checkCollision(int px, int py, int piece, int rot) {
    uint16_t shape = TETROMINOES[piece][rot % 4];
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            if (shape & (0x8000 >> (r * 4 + c))) {
                int bx = px + c;
                int by = py + r;
                if (bx < 0 || bx >= 10 || by >= 20) return true;
                if (by >= 0 && board[by][bx]) return true;
            }
        }
    }
    return false;
}

void GameEngine::lockPiece() {
    uint16_t shape = TETROMINOES[currentPiece][pieceRotation % 4];
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            if (shape & (0x8000 >> (r * 4 + c))) {
                int bx = pieceX + c;
                int by = pieceY + r;
                if (by >= 0 && by < 20 && bx >= 0 && bx < 10) {
                    board[by][bx] = 1;
                }
            }
        }
    }
    play8BitSound(62, 40, 80);
    clearLines();
    spawnPiece();
}

void GameEngine::clearLines() {
    int cleared = 0;
    for (int r = 19; r >= 0; r--) {
        bool full = true;
        for (int c = 0; c < 10; c++) {
            if (!board[r][c]) { full = false; break; }
        }
        if (full) {
            cleared++;
            for (int k = r; k > 0; k--) {
                for (int c = 0; c < 10; c++) {
                    board[k][c] = board[k - 1][c];
                }
            }
            for (int c = 0; c < 10; c++) board[0][c] = 0;
            r++;
        }
    }

    if (cleared > 0) {
        stackLines += cleared;
        stackLevelLines += cleared;
        int baseScore = (cleared == 1) ? 100 : (cleared == 2) ? 300 : (cleared == 3) ? 500 : 800;
        stackScore += baseScore * stackLevel;

        // 10줄 클리어마다 레벨업 (무한 가속)
        if (stackLevelLines >= 10) {
            stackLevelLines -= 10;
            stackLevel++;
            int calcDrop = 500 - (stackLevel - 1) * 40;
            dropInterval = (calcDrop < 60) ? 60 : calcDrop;
            play8BitSound(88, 300, 80); // 레벨업 팡파레!
        } else {
            if (cleared >= 4) {
                play8BitSound(84, 250, 80);
            } else {
                play8BitSound(76, 120, 80);
            }
        }
    }
}

void GameEngine::updateBlockStack(U8G2& u8g2) {
    if (!stackGameOver) {
        if (millis() - lastDropTime >= dropInterval) {
            lastDropTime = millis();
            if (!checkCollision(pieceX, pieceY + 1, currentPiece, pieceRotation)) {
                pieceY++;
            } else {
                lockPiece();
            }
        }
    }

    // 상단 헤더 (Yellow 영역 Y: 0 ~ 14)
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(2, 9, "[BLOCK STACK]");
    u8g2.setFont(u8g2_font_4x6_tf);
    u8g2.drawStr(82, 8, "[5s Exit]");
    u8g2.drawHLine(0, 14, 128);

    // 10x11 보드
    int boardX = 4;
    int boardY = 17;
    int bSize = 4;
    u8g2.drawFrame(boardX - 1, boardY - 1, (10 * bSize) + 2, (11 * bSize) + 2);

    for (int r = 9; r < 20; r++) {
        for (int c = 0; c < 10; c++) {
            if (board[r][c]) {
                u8g2.drawBox(boardX + (c * bSize), boardY + ((r - 9) * bSize), bSize - 1, bSize - 1);
            }
        }
    }

    if (!stackGameOver) {
        uint16_t shape = TETROMINOES[currentPiece][pieceRotation % 4];
        for (int r = 0; r < 4; r++) {
            for (int c = 0; c < 4; c++) {
                if (shape & (0x8000 >> (r * 4 + c))) {
                    int by = pieceY + r;
                    if (by >= 9 && by < 20) {
                        u8g2.drawBox(boardX + ((pieceX + c) * bSize), boardY + ((by - 9) * bSize), bSize - 1, bSize - 1);
                    }
                }
            }
        }
    }

    // 우측 패널 정보
    u8g2.setFont(u8g2_font_5x7_tf);
    char buf[32];
    snprintf(buf, sizeof(buf), "Sc:%05d", stackScore);
    u8g2.drawStr(48, 25, buf);
    snprintf(buf, sizeof(buf), "LV:%02d L:%d", stackLevel, stackLines);
    u8g2.drawStr(48, 35, buf);

    u8g2.drawStr(48, 47, "Next:");
    uint16_t nShape = TETROMINOES[nextPiece][0];
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            if (nShape & (0x8000 >> (r * 4 + c))) {
                u8g2.drawBox(78 + (c * 3), 40 + (r * 3), 2, 2);
            }
        }
    }

    u8g2.setFont(u8g2_font_4x6_tf);
    u8g2.drawStr(48, 55, "Rot:Move/1s:Drop");
    u8g2.drawStr(48, 62, "Click:Rotate");

    if (stackGameOver) {
        u8g2.setFont(u8g2_font_galmuri9);
        u8g2.setDrawColor(0);
        u8g2.drawBox(10, 22, 108, 28);
        u8g2.setDrawColor(1);
        u8g2.drawFrame(10, 22, 108, 28);
        u8g2.drawUTF8(14, 35, "GAME OVER (Click)");
        char finalStr[32];
        snprintf(finalStr, sizeof(finalStr), "FINAL: LV.%02d", stackLevel);
        u8g2.drawUTF8(14, 47, finalStr);
    }
}

void GameEngine::handleBlockStackInput(EncoderEvent event) {
    if (stackGameOver) {
        if (event == ENC_BUTTON_CLICK) initBlockStack();
        return;
    }

    if (event == ENC_ROTATE_CW) {
        if (!checkCollision(pieceX + 1, pieceY, currentPiece, pieceRotation)) {
            pieceX++;
        }
    } else if (event == ENC_ROTATE_CCW) {
        if (!checkCollision(pieceX - 1, pieceY, currentPiece, pieceRotation)) {
            pieceX--;
        }
    } else if (event == ENC_BUTTON_CLICK) {
        int nextRot = (pieceRotation + 1) % 4;
        if (!checkCollision(pieceX, pieceY, currentPiece, nextRot)) {
            pieceRotation = nextRot;
            play8BitSound(86, 30, 80);
        }
    } else if (event == ENC_BUTTON_LONG) {
        while (!checkCollision(pieceX, pieceY + 1, currentPiece, pieceRotation)) {
            pieceY++;
        }
        lockPiece();
        play8BitSound(50, 60, 80);
    }
}

// ==========================================
// 4. 벽돌깨기 (1976 Brick Breaker - 무한 웨이브 서바이벌)
// ==========================================

void GameEngine::initBrick() {
    brickScore = 0;
    brickLives = 5;
    brickWave = 1;
    brickPaddleW = 18;
    brickPaddleX = 54;
    brickGameOver = false;
    resetBrickLevel();
}

void GameEngine::resetBrickLevel() {
    int curW = 18 - (brickWave - 1);
    brickPaddleW = (curW < 10) ? 10 : curW;

    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 8; c++) {
            if (brickWave == 1) {
                bricks[r][c] = 1;
            } else if (brickWave <= 3) {
                bricks[r][c] = (r == 0) ? 2 : 1;
            } else if (brickWave <= 5) {
                bricks[r][c] = (r == 0) ? 3 : ((r == 1) ? 2 : 1);
            } else {
                bricks[r][c] = (r <= 1) ? 3 : ((r == 2) ? 2 : 1);
            }
        }
    }
    brickBallStuck = true;
    brickBallX = brickPaddleX + (brickPaddleW / 2);
    brickBallY = 56;
    float baseSpd = 1.4f + (float)(brickWave - 1) * 0.12f;
    if (baseSpd > 2.8f) baseSpd = 2.8f;
    brickBallVX = baseSpd;
    brickBallVY = -baseSpd;
}

void GameEngine::updateBrick(U8G2& u8g2) {
    u8g2.setFont(u8g2_font_galmuri9);

    // 상단 헤더 (Yellow 영역 Y: 0 ~ 14)
    char hdr[32];
    snprintf(hdr, sizeof(hdr), "W:%02d S:%04d L:%d", brickWave, brickScore, brickLives);
    u8g2.drawUTF8(2, 9, hdr);
    u8g2.setFont(u8g2_font_4x6_tf);
    u8g2.drawStr(90, 8, "[5s Exit]");
    u8g2.drawHLine(0, 14, 128);

    if (!brickGameOver) {
        if (brickBallStuck) {
            brickBallX = brickPaddleX + (brickPaddleW / 2);
            brickBallY = 56;
        } else {
            brickBallX += brickBallVX;
            brickBallY += brickBallVY;

            // 좌우 벽 충돌
            if (brickBallX <= 1.0f) {
                brickBallX = 1.0f;
                brickBallVX = fabsf(brickBallVX);
                play8BitSound(52, 25, 80);
            } else if (brickBallX >= 125.0f) {
                brickBallX = 125.0f;
                brickBallVX = -fabsf(brickBallVX);
                play8BitSound(52, 25, 80);
            }

            // 상단 벽 충돌
            if (brickBallY <= 17.0f) {
                brickBallY = 17.0f;
                brickBallVY = fabsf(brickBallVY);
                play8BitSound(52, 25, 80);
            }

            // 패들 충돌 검사
            if (brickBallY >= 56.0f && brickBallY <= 59.0f) {
                if (brickBallX >= brickPaddleX - 2 && brickBallX <= brickPaddleX + brickPaddleW + 2) {
                    brickBallVY = -fabsf(brickBallVY);
                    float halfW = (float)brickPaddleW / 2.0f;
                    float offset = (brickBallX - (brickPaddleX + halfW)) / halfW;
                    brickBallVX = offset * 1.8f;
                    play8BitSound(60, 30, 80);
                }
            }

            // 벽돌 충돌 검사
            int remaining = 0;
            for (int r = 0; r < 4; r++) {
                for (int c = 0; c < 8; c++) {
                    if (bricks[r][c] > 0) {
                        remaining++;
                        int bx = 8 + (c * 14);
                        int by = 17 + (r * 5);
                        if (brickBallX >= bx && brickBallX <= bx + 13 && brickBallY >= by && brickBallY <= by + 4) {
                            bricks[r][c]--;
                            brickBallVY = -brickBallVY;
                            brickScore += (4 - r) * 20 * brickWave;
                            uint8_t pitch = 65 + (3 - r) * 5 + (bricks[r][c] * 4);
                            play8BitSound(pitch, 40, 80);
                            if (bricks[r][c] == 0) remaining--;
                            break;
                        }
                    }
                }
            }

            // 웨이브 클리어 -> 다음 웨이브 자동 리스폰!
            if (remaining == 0) {
                brickWave++;
                brickScore += 500 * brickWave;
                play8BitSound(84, 300, 80); // 웨이브 클리어 팡파레!
                resetBrickLevel();
            }

            // 바닥 추락
            if (brickBallY > 64) {
                brickLives--;
                play8BitSound(44, 250, 80);
                if (brickLives <= 0) {
                    brickGameOver = true;
                } else {
                    brickBallStuck = true;
                }
            }
        }
    }

    // 벽돌 렌더링 (내구도에 따른 텍스처)
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 8; c++) {
            uint8_t hp = bricks[r][c];
            if (hp > 0) {
                int bx = 8 + (c * 14);
                int by = 17 + (r * 5);
                if (hp == 1) {
                    u8g2.drawBox(bx, by, 13, 4);
                } else if (hp == 2) {
                    u8g2.drawBox(bx, by, 13, 4);
                    u8g2.setDrawColor(0);
                    u8g2.drawHLine(bx + 2, by + 1, 9);
                    u8g2.setDrawColor(1);
                } else {
                    u8g2.drawFrame(bx, by, 13, 4);
                    u8g2.drawBox(bx + 3, by + 1, 7, 2);
                }
            }
        }
    }

    // 패들 렌더링
    u8g2.drawBox(brickPaddleX, 58, brickPaddleW, 2);

    // 공 렌더링 (2x2)
    u8g2.drawBox((int)brickBallX, (int)brickBallY, 2, 2);

    if (brickGameOver) {
        u8g2.setFont(u8g2_font_galmuri9);
        u8g2.setDrawColor(0);
        u8g2.drawBox(10, 22, 108, 28);
        u8g2.setDrawColor(1);
        u8g2.drawFrame(10, 22, 108, 28);
        u8g2.drawUTF8(14, 35, "GAME OVER (Click)");
        char finalStr[32];
        snprintf(finalStr, sizeof(finalStr), "FINAL: WAVE %02d", brickWave);
        u8g2.drawUTF8(14, 47, finalStr);
    }
}

void GameEngine::handleBrickInput(EncoderEvent event) {
    if (brickGameOver) {
        if (event == ENC_BUTTON_CLICK) initBrick();
        return;
    }

    int maxX = 126 - brickPaddleW;
    if (event == ENC_ROTATE_CW) {
        brickPaddleX += 5;
        if (brickPaddleX > maxX) brickPaddleX = maxX;
    } else if (event == ENC_ROTATE_CCW) {
        brickPaddleX -= 5;
        if (brickPaddleX < 2) brickPaddleX = 2;
    } else if (event == ENC_BUTTON_CLICK) {
        if (brickBallStuck) {
            brickBallStuck = false;
            play8BitSound(72, 50, 80);
        }
    }
}

// ==========================================
// 5. 스네이크 (Snake - 15먹이 무한 스테이지 & 동적 장애물)
// ==========================================

void GameEngine::generateObstacles(int stage) {
    snakeObstacleCount = 0;
    if (stage <= 1) return;

    if (stage == 2) {
        // 4개 분산 장애물
        snakeObstacles[0] = { 7, 3 };
        snakeObstacles[1] = { 22, 3 };
        snakeObstacles[2] = { 7, 7 };
        snakeObstacles[3] = { 22, 7 };
        snakeObstacleCount = 4;
    } else if (stage == 3) {
        // 8개 기둥형 장애물
        snakeObstacles[0] = { 8, 2 };  snakeObstacles[1] = { 8, 3 };
        snakeObstacles[2] = { 8, 7 };  snakeObstacles[3] = { 8, 8 };
        snakeObstacles[4] = { 21, 2 }; snakeObstacles[5] = { 21, 3 };
        snakeObstacles[6] = { 21, 7 }; snakeObstacles[7] = { 21, 8 };
        snakeObstacleCount = 8;
    } else if (stage == 4) {
        // 12개 십자 분산 장애물
        snakeObstacles[0] = { 8, 2 };   snakeObstacles[1] = { 8, 8 };
        snakeObstacles[2] = { 21, 2 };  snakeObstacles[3] = { 21, 8 };
        snakeObstacles[4] = { 14, 2 };  snakeObstacles[5] = { 15, 2 };
        snakeObstacles[6] = { 14, 8 };  snakeObstacles[7] = { 15, 8 };
        snakeObstacles[8] = { 6, 5 };   snakeObstacles[9] = { 7, 5 };
        snakeObstacles[10] = { 22, 5 }; snakeObstacles[11] = { 23, 5 };
        snakeObstacleCount = 12;
    } else {
        // 16개 코너 L자 및 미로형 장애물
        snakeObstacles[0] = { 5, 2 };   snakeObstacles[1] = { 6, 2 };   snakeObstacles[2] = { 5, 3 };
        snakeObstacles[3] = { 23, 2 };  snakeObstacles[4] = { 24, 2 };  snakeObstacles[5] = { 24, 3 };
        snakeObstacles[6] = { 5, 7 };   snakeObstacles[7] = { 5, 8 };   snakeObstacles[8] = { 6, 8 };
        snakeObstacles[9] = { 24, 7 };  snakeObstacles[10] = { 23, 8 }; snakeObstacles[11] = { 24, 8 };
        snakeObstacles[12] = { 14, 3 }; snakeObstacles[13] = { 15, 3 };
        snakeObstacles[14] = { 14, 7 }; snakeObstacles[15] = { 15, 7 };
        snakeObstacleCount = 16;
    }
}

void GameEngine::initSnake() {
    snakeScore = 0;
    snakeLives = 5;
    snakeStage = 1;
    snakeStageFoodCount = 0;
    snakeMoveInterval = 130;
    snakeGameOver = false;
    generateObstacles(1);
    resetSnakeRound();
}

void GameEngine::resetSnakeRound() {
    snakeLen = 4;
    snakeDirX = 1;
    snakeDirY = 0;
    lastSnakeMoveTime = millis();

    for (int i = 0; i < snakeLen; i++) {
        snakeBody[i].x = 10 - i;
        snakeBody[i].y = 5;
    }
    spawnSnakeFood();
}

void GameEngine::spawnSnakeFood() {
    for (int retry = 0; retry < 100; retry++) {
        snakeFood.x = 1 + (rand() % 28);
        snakeFood.y = 1 + (rand() % 9);
        bool valid = true;

        for (int o = 0; o < snakeObstacleCount; o++) {
            if (snakeFood.x == snakeObstacles[o].x && snakeFood.y == snakeObstacles[o].y) {
                valid = false;
                break;
            }
        }
        if (!valid) continue;

        for (int i = 0; i < snakeLen; i++) {
            if (snakeFood.x == snakeBody[i].x && snakeFood.y == snakeBody[i].y) {
                valid = false;
                break;
            }
        }
        if (valid) break;
    }
}

void GameEngine::updateSnake(U8G2& u8g2) {
    u8g2.setFont(u8g2_font_galmuri9);

    // 상단 헤더 (Yellow 영역 Y: 0 ~ 14)
    char hdr[32];
    snprintf(hdr, sizeof(hdr), "S:%02d SC:%03d L:%d", snakeStage, snakeScore, snakeLives);
    u8g2.drawUTF8(2, 9, hdr);
    u8g2.setFont(u8g2_font_4x6_tf);
    u8g2.drawStr(90, 8, "[5s Exit]");

    // 필드 외곽 프레임
    u8g2.drawFrame(0, 16, 128, 48);

    // 뱀 이동 로직
    if (!snakeGameOver && (millis() - lastSnakeMoveTime >= snakeMoveInterval)) {
        lastSnakeMoveTime = millis();

        Point nextHead = { (int8_t)(snakeBody[0].x + snakeDirX), (int8_t)(snakeBody[0].y + snakeDirY) };
        bool hit = false;

        // 벽 충돌 검사
        if (nextHead.x < 0 || nextHead.x >= 30 || nextHead.y < 0 || nextHead.y >= 11) {
            hit = true;
        }

        // 장애물 충돌 검사
        if (!hit) {
            for (int o = 0; o < snakeObstacleCount; o++) {
                if (nextHead.x == snakeObstacles[o].x && nextHead.y == snakeObstacles[o].y) {
                    hit = true;
                    break;
                }
            }
        }

        // 자기 몸 충돌 검사
        if (!hit) {
            for (int i = 0; i < snakeLen; i++) {
                if (snakeBody[i].x == nextHead.x && snakeBody[i].y == nextHead.y) {
                    hit = true;
                    break;
                }
            }
        }

        if (hit) {
            snakeLives--;
            play8BitSound(44, 200, 80);
            if (snakeLives <= 0) {
                snakeGameOver = true;
                play8BitSound(40, 400, 80);
            } else {
                resetSnakeRound();
            }
        } else {
            // 먹이 섭취
            if (nextHead.x == snakeFood.x && nextHead.y == snakeFood.y) {
                if (snakeLen < 60) snakeLen++;
                snakeScore += 10;
                snakeStageFoodCount++;

                // 15개 먹이 섭취 시 스테이지 승급!
                if (snakeStageFoodCount >= 15) {
                    snakeStage++;
                    snakeStageFoodCount = 0;
                    int calcInterval = 130 - (snakeStage - 1) * 14;
                    snakeMoveInterval = (calcInterval < 45) ? 45 : calcInterval;
                    generateObstacles(snakeStage);
                    play8BitSound(88, 300, 80); // 스테이지 클리어 팡파레!
                    spawnSnakeFood();
                } else {
                    uint8_t pitch = 76 + min(12, snakeScore / 20);
                    play8BitSound(pitch, 50, 80);
                    spawnSnakeFood();
                }
            }

            // 몸통 전진
            for (int i = snakeLen - 1; i > 0; i--) {
                snakeBody[i] = snakeBody[i - 1];
            }
            snakeBody[0] = nextHead;
        }
    }

    int fieldX = 4;
    int fieldY = 18;
    int cSize = 4;

    // 장애물 렌더링 (가운데 점 뚫린 바위 블록)
    for (int o = 0; o < snakeObstacleCount; o++) {
        int ox = fieldX + (snakeObstacles[o].x * cSize);
        int oy = fieldY + (snakeObstacles[o].y * cSize);
        u8g2.drawBox(ox, oy, cSize - 1, cSize - 1);
        u8g2.setDrawColor(0);
        u8g2.drawPixel(ox + 1, oy + 1);
        u8g2.setDrawColor(1);
    }

    // 먹이 그리기 (깜빡이는 2x2 점)
    u8g2.drawBox(fieldX + (snakeFood.x * cSize) + 1, fieldY + (snakeFood.y * cSize) + 1, 2, 2);

    // 뱀 몸통 그리기
    for (int i = 0; i < snakeLen; i++) {
        int sx = fieldX + (snakeBody[i].x * cSize);
        int sy = fieldY + (snakeBody[i].y * cSize);
        if (i == 0) {
            u8g2.drawBox(sx, sy, cSize - 1, cSize - 1);
        } else {
            u8g2.drawFrame(sx, sy, cSize - 1, cSize - 1);
        }
    }

    if (snakeGameOver) {
        u8g2.setFont(u8g2_font_galmuri9);
        u8g2.setDrawColor(0);
        u8g2.drawBox(10, 22, 108, 28);
        u8g2.setDrawColor(1);
        u8g2.drawFrame(10, 22, 108, 28);
        u8g2.drawUTF8(14, 35, "GAME OVER (Click)");
        char finalStr[32];
        snprintf(finalStr, sizeof(finalStr), "FINAL: STAGE %02d", snakeStage);
        u8g2.drawUTF8(14, 47, finalStr);
    }
}

void GameEngine::handleSnakeInput(EncoderEvent event) {
    if (snakeGameOver) {
        if (event == ENC_BUTTON_CLICK) initSnake();
        return;
    }

    if (event == ENC_ROTATE_CW) {
        int8_t oldX = snakeDirX;
        snakeDirX = -snakeDirY;
        snakeDirY = oldX;
        play8BitSound(84, 10, 80);
    } else if (event == ENC_ROTATE_CCW) {
        int8_t oldX = snakeDirX;
        snakeDirX = snakeDirY;
        snakeDirY = -oldX;
        play8BitSound(84, 10, 80);
    }
}


