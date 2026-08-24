#include "led_indicator.h"
#include "config.h"
#include <Adafruit_NeoPixel.h>
#include <Preferences.h>

static Adafruit_NeoPixel pixel(NUM_LEDS, PIN_RGB_LED, NEO_GRB + NEO_KHZ800);

LEDState LEDIndicator::currentState = LED_BOOTING;
unsigned long LEDIndicator::lastUpdate = 0;
bool LEDIndicator::blinkState = false;
bool LEDIndicator::enabled = true;

void LEDIndicator::begin() {
    Preferences prefs;
    prefs.begin("led_cfg", true);
    enabled = prefs.getBool("en", true);
    prefs.end();

    pixel.begin();
    pixel.setBrightness(50); // 눈부심 없는 은은한 밝기
    if (enabled) {
        pixel.setPixelColor(0, pixel.Color(255, 100, 0)); // 부팅 시 주황
        pixel.show();
    } else {
        pixel.clear();
        pixel.show();
    }
}

void LEDIndicator::setEnabled(bool enable) {
    if (enabled != enable) {
        enabled = enable;
        Preferences prefs;
        prefs.begin("led_cfg", false);
        prefs.putBool("en", enabled);
        prefs.end();

        if (!enabled) {
            pixel.clear();
            pixel.show();
        } else {
            // 현재 상태에 맞게 복원 점등
            LEDState st = currentState;
            currentState = LED_OFF; // 강제 갱신 트리거
            setState(st);
        }
    }
}

bool LEDIndicator::isEnabled() {
    return enabled;
}

void LEDIndicator::setState(LEDState state) {
    if (currentState == state) return;
    currentState = state;
    if (!enabled) {
        pixel.clear();
        pixel.show();
        return;
    }

    if (state == LED_NORMAL) {
        pixel.setPixelColor(0, pixel.Color(255, 100, 0)); // 대기 상태: 레트로 오렌지
        pixel.show();
    } else if (state == LED_PLAYING) {
        pixel.setPixelColor(0, pixel.Color(180, 180, 180)); // 재생 중: 은은한 하얀색
        pixel.show();
    } else if (state == LED_AP_MODE) {
        pixel.setPixelColor(0, pixel.Color(0, 120, 255)); // 차분한 블루
        pixel.show();
    } else if (state == LED_BOOTING) {
        pixel.setPixelColor(0, pixel.Color(255, 100, 0));
        pixel.show();
    } else if (state == LED_OFF) {
        pixel.clear();
        pixel.show();
    }
}

void LEDIndicator::pulseMIDI() {
    // 자원 낭비 및 인터럽트 블로킹 방지를 위해 번쩍임 이펙트 비활성화
}

void LEDIndicator::update() {
    if (!enabled) return;

    if (currentState == LED_WIFI_CONNECTING) {
        // Wi-Fi 연결 중일 때만 300ms 간격으로 파란색 깜빡임
        unsigned long now = millis();
        if (now - lastUpdate > 300) {
            lastUpdate = now;
            blinkState = !blinkState;
            pixel.setPixelColor(0, blinkState ? pixel.Color(0, 150, 255) : 0);
            pixel.show();
        }
    }
}
