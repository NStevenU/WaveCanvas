#pragma once

#include <Arduino.h>

// ==========================================
// 1. 하드웨어 핀맵 설정 (Hardware Pinout)
// ==========================================

// MAX3232 RS-232 to TTL UART (구형 노트북 SoftMPU / COM 포트)
#define PIN_MIDI_RX 18
#define PIN_MIDI_TX 17

// PCM5102A I2S DAC
#define PIN_I2S_BCLK 15
#define PIN_I2S_LRC 16
#define PIN_I2S_DOUT 7
// ※ PCM5102A 모듈의 SCK/SCL 핀은 GND에 연결 (내부 PLL 모드)

// SSD1306 0.96" OLED (I2C)
#define PIN_I2C_SDA 8
#define PIN_I2C_SCL 9
#define OLED_SCREEN_WIDTH 128
#define OLED_SCREEN_HEIGHT 64
#define OLED_I2C_ADDR 0x3C

// 로터리 엔코더 (EC11 Rotary Encoder)
#define PIN_ENC_CLK 4
#define PIN_ENC_DT 5
#define PIN_ENC_SW 6

// YD-ESP32-S3 온보드 WS2812 RGB LED
#define PIN_RGB_LED 48
#define NUM_LEDS 1

// 외장 모노 스피커 모듈 감지 핀 (GND 접촉 시 모노 자동 전환)
#define PIN_MONO_DETECT 47

// ==========================================
// 2. 오디오 및 MIDI 설정
// ==========================================
#define AUDIO_SAMPLE_RATE 44100
#define AUDIO_BUFFER_SIZE 512 // 버퍼당 샘플 프레임 수 (11.6ms 최적 지연시간 + 락 오버헤드 50% 절감)
#define AUDIO_MAX_VOICES 32   // 최대 동시 발음수 32보이스 (고음질 오케스트라 완벽 수용)
#define DEFAULT_MIDI_BAUD 38400 // SoftMPU / DOS 시리얼 기본 속도
#define ALT_MIDI_BAUD 31250     // 표준 MIDI 통신 속도

// ==========================================
// 3. Wi-Fi & 웹 관리자 기본값
// ==========================================
#define DEFAULT_AP_SSID "WaveCanvas-NanoRS"
#define DEFAULT_AP_PASS "" // 기본 오픈 AP (비밀번호 없음)
#define WEB_SERVER_PORT 80

// 기본 로드 사운드폰트
#define DEFAULT_SF2_FILE "/CT4MGM.SF2"
