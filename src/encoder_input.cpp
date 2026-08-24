#include "encoder_input.h"
#include "config.h"

volatile int EncoderInput::encoderDelta = 0;
volatile int8_t EncoderInput::encoderAcc = 0;
volatile uint8_t EncoderInput::encoderOldState = 0b11;
bool EncoderInput::buttonState = HIGH;
unsigned long EncoderInput::buttonPressTime = 0;
bool EncoderInput::longPressHandled = false;
EncoderEvent EncoderInput::queuedEvent = ENC_NONE;

// 2비트 그레이 코드 상태 전이 테이블 (디바운싱 및 채터링 100% 필터링)
static const int8_t ENC_TABLE[16] = {
    0,  -1,   1,   0,
    1,   0,   0,  -1,
   -1,   0,   0,   1,
    0,   1,  -1,   0
};

void IRAM_ATTR EncoderInput::isrEncoder() {
    uint8_t clk = digitalRead(PIN_ENC_CLK);
    uint8_t dt  = digitalRead(PIN_ENC_DT);
    uint8_t newState = (clk << 1) | dt;

    int8_t step = ENC_TABLE[(encoderOldState << 2) | newState];
    encoderAcc += step;

    // 디텐트(안정 휴지 상태 0b11)에 도달했을 때 누적 회전 방향 판정
    if (newState == 0b11 && encoderOldState != 0b11) {
        if (encoderAcc > 0) {
            encoderDelta++;
        } else if (encoderAcc < 0) {
            encoderDelta--;
        }
        encoderAcc = 0;
    }

    encoderOldState = newState;
}

void EncoderInput::begin() {
    pinMode(PIN_ENC_CLK, INPUT_PULLUP);
    pinMode(PIN_ENC_DT, INPUT_PULLUP);
    pinMode(PIN_ENC_SW, INPUT_PULLUP);

    uint8_t clk = digitalRead(PIN_ENC_CLK);
    uint8_t dt  = digitalRead(PIN_ENC_DT);
    encoderOldState = (clk << 1) | dt;

    attachInterrupt(digitalPinToInterrupt(PIN_ENC_CLK), isrEncoder, CHANGE);
    attachInterrupt(digitalPinToInterrupt(PIN_ENC_DT), isrEncoder, CHANGE);
}

EncoderEvent EncoderInput::getEvent() {
    EncoderEvent ev = queuedEvent;
    queuedEvent = ENC_NONE;
    return ev;
}

void EncoderInput::update() {
    // 1. 회전 이벤트 처리 (인터럽트와의 레이스 컨디션 방지를 위한 원자적 처리)
    int delta = 0;
    noInterrupts();
    if (encoderDelta > 0) {
        delta = 1;
        encoderDelta--;
    } else if (encoderDelta < 0) {
        delta = -1;
        encoderDelta++;
    }
    interrupts();

    if (delta > 0) {
        queuedEvent = ENC_ROTATE_CW;
        return;
    } else if (delta < 0) {
        queuedEvent = ENC_ROTATE_CCW;
        return;
    }

    // 2. 버튼 입력 디바운싱 및 롱프레스 감지
    bool currentBtn = digitalRead(PIN_ENC_SW);
    static bool veryLongHandled = false;

    if (buttonState == HIGH && currentBtn == LOW) {
        // 누르기 시작 (Active Low)
        buttonState = LOW;
        buttonPressTime = millis();
        longPressHandled = false;
        veryLongHandled = false;
    } else if (buttonState == LOW && currentBtn == LOW) {
        unsigned long dur = millis() - buttonPressTime;
        // 5.0초 이상 = 초장기 롱프레스 (게임 탈출용)
        if (!veryLongHandled && dur >= 5000) {
            queuedEvent = ENC_BUTTON_VERY_LONG;
            veryLongHandled = true;
        }
        // 1.0초 이상 = 일반 롱프레스 (하드드롭 / MIDI Panic / Easter Egg 진입용)
        else if (!longPressHandled && dur >= 1000) {
            queuedEvent = ENC_BUTTON_LONG;
            longPressHandled = true;
        }
    } else if (buttonState == LOW && currentBtn == HIGH) {
        // 손을 뗌
        buttonState = HIGH;
        if (!longPressHandled && !veryLongHandled && (millis() - buttonPressTime >= 50)) { // 50ms 이상 디바운스
            queuedEvent = ENC_BUTTON_CLICK;
        }
    }
}

bool EncoderInput::isButtonPressed() {
    return (buttonState == LOW);
}

unsigned long EncoderInput::getButtonPressDuration() {
    if (buttonState == LOW) {
        return millis() - buttonPressTime;
    }
    return 0;
}

