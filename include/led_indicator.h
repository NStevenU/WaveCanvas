#pragma once

#include <Arduino.h>

enum LEDState {
    LED_BOOTING,
    LED_NORMAL,           // 정상 대기 (주황색 점등)
    LED_PLAYING,          // MIDI 재생 중 (하얀색 점등)
    LED_WIFI_CONNECTING,  // Wi-Fi 공유기 연결 시도 중 (파란색 깜빡임)
    LED_AP_MODE,          // Wi-Fi AP 모드 (파란색 점등)
    LED_OFF
};

class LEDIndicator {
public:
    static void begin();
    static void setState(LEDState state);
    static void pulseMIDI();
    static void update();
    static void setEnabled(bool enable);
    static bool isEnabled();

private:
    static LEDState currentState;
    static unsigned long lastUpdate;
    static bool blinkState;
    static bool enabled;
};
