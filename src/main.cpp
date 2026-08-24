#include <Arduino.h>
#include <LittleFS.h>

#include "config.h"
#include "led_indicator.h"
#include "audio_engine.h"
#include "midi_parser.h"
#include "midi_sequencer.h"
#include "encoder_input.h"
#include "display_ui.h"
#include "wifi_manager.h"
#include "web_manager.h"
#include "time_manager.h"

static bool lastConfirmedMonoState = HIGH;
static bool lastSampledMonoState = HIGH;
static unsigned long monoPinTransitionTime = 0;

void setup() {
    Serial.begin(115200);
    Serial.setTxTimeoutMs(0);
    delay(500);

    pinMode(PIN_MONO_DETECT, INPUT_PULLUP);
    bool isPlugged = (digitalRead(PIN_MONO_DETECT) == LOW);
    AudioEngine::setHardwareMonoDetected(isPlugged);
    lastConfirmedMonoState = isPlugged ? LOW : HIGH;
    lastSampledMonoState = lastConfirmedMonoState;

    LEDIndicator::begin();
    LEDIndicator::setState(LED_BOOTING);

    LittleFS.begin(true);

    DisplayUI::begin();
    EncoderInput::begin();
    MIDIParser::begin(DEFAULT_MIDI_BAUD);

    if (AudioEngine::begin()) {
        if (LittleFS.exists(DEFAULT_SF2_FILE)) {
            AudioEngine::loadSoundFontAsync(DEFAULT_SF2_FILE); // 16KB 전용 스택 태스크에서 안전하게 비동기 로드
        }
    }

    MIDISequencer::begin();
    WiFiManager::begin();
    WebManager::begin();
    TimeManager::begin();
}

void loop() {
    // 외장 스피커 모듈 하드웨어 감지 (안정화 디바운스 50ms)
    unsigned long now = millis();
    bool pinState = digitalRead(PIN_MONO_DETECT);
    if (pinState != lastSampledMonoState) {
        lastSampledMonoState = pinState;
        monoPinTransitionTime = now;
    } else if ((now - monoPinTransitionTime >= 50) && (pinState != lastConfirmedMonoState)) {
        lastConfirmedMonoState = pinState;
        bool isDetected = (pinState == LOW);
        AudioEngine::setHardwareMonoDetected(isDetected);
        if (isDetected) {
            DisplayUI::showToast(DisplayUI::isKoreanMode() ? "외부 스피커 연결" : "Ext Spk Connected", 2000);
        } else {
            DisplayUI::showToast(DisplayUI::isKoreanMode() ? "외부 스피커 해제" : "Ext Spk Removed", 2000);
        }
    }

    EncoderInput::update();
    EncoderEvent ev = EncoderInput::getEvent();
    if (ev != ENC_NONE) {
        DisplayUI::handleEncoderEvent(ev);
    }

    //MIDIParser::update();
    TimeManager::update();
    LEDIndicator::update();
    AudioEngine::flushVolumeNVS();

    yield();
}
