#pragma once

#include <Arduino.h>

enum EncoderEvent {
    ENC_NONE,
    ENC_ROTATE_CW,         // 시계방향 회전
    ENC_ROTATE_CCW,        // 반시계방향 회전
    ENC_BUTTON_CLICK,      // 짧은 클릭 (< 1.0초)
    ENC_BUTTON_LONG,       // 1.0초 이상 중기 롱프레스 (수동 모드 순환 / 하드드롭 / 가상피아노)
    ENC_BUTTON_PANIC,      // 3.0초 이상 장기 롱프레스 (MIDI Panic 긴급 리셋)
    ENC_BUTTON_VERY_LONG   // 5.0초 이상 초장기 롱프레스 (게임 탈출)
};

class EncoderInput {
public:
    static void begin();
    static EncoderEvent getEvent();
    static void update();
    static bool isButtonPressed();
    static unsigned long getButtonPressDuration();

private:
    static volatile int encoderDelta;
    static volatile int8_t encoderAcc;
    static volatile uint8_t encoderOldState;
    static bool buttonState;
    static unsigned long buttonPressTime;
    static bool longPressHandled;
    static EncoderEvent queuedEvent;

    static void IRAM_ATTR isrEncoder();
};
