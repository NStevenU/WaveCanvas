#pragma once

#include <Arduino.h>

struct ChannelStatus {
    uint8_t program;        // 현재 악기 번호 (0 ~ 127)
    uint8_t volume;         // CC 7 볼륨 (0 ~ 127)
    uint8_t expression;     // CC 11 익스프레션 (0 ~ 127)
    uint8_t vuLevel;        // OLED용 실시간 감쇠 레벨 미터 (0 ~ 15)
    uint32_t lastNoteTime;  // 마지막 노트 발생 시간
};

enum SynthMode {
    SYNTH_MODE_GM,
    SYNTH_MODE_GS,
    SYNTH_MODE_MT32
};

enum SynthPolicy {
    SYNTH_POLICY_AUTO = 0,    // 스마트 자동 감지 (기본값)
    SYNTH_POLICY_MANUAL = 1   // 수동 고정 모드
};

enum ManualSubMode {
    MANUAL_MODE_GM = 0,
    MANUAL_MODE_GS = 1,
    MANUAL_MODE_MT32 = 2
};

class MIDIParser {
public:
    static void begin(uint32_t baudRate = 38400);
    static void update(); // UART 수신 및 파싱 루프
    static void parseByte(uint8_t b);
    static void parseSysExByte(uint8_t b);

    // 디스플레이 UI용 데이터 접근자 및 상태 제어자
    static const ChannelStatus& getChannelStatus(uint8_t channel);
    static void setChannelProgram(uint8_t ch, uint8_t prog);
    static void setChannelVolume(uint8_t ch, uint8_t vol);
    static void setChannelExpression(uint8_t ch, uint8_t expr);
    static void setChannelVU(uint8_t ch, uint8_t vu);
    static void setChannelLastNoteTime(uint8_t ch, uint32_t t);
    static void resetChannelStatus(uint8_t ch);
    static const char* getInstrumentName(uint8_t program, bool isDrum = false);
    static uint8_t getLastActiveChannel();
    static void setLastActiveChannel(uint8_t ch);

    static void setBaudRate(uint32_t baud);
    static uint32_t getBaudRate();
    static void clearAllVU();
    static bool isMIDIActive();

    static SynthMode getSynthMode();
    static void setSynthMode(SynthMode mode);
    static const char* getSynthModeString();

    static SynthPolicy getSynthPolicy();
    static void setSynthPolicy(SynthPolicy policy, bool save = true);
    static ManualSubMode getManualSubMode();
    static void setManualSubMode(ManualSubMode mode, bool save = true);
    static void cycleManualSubMode();
    static const char* getIndicatorString();

private:
    static uint32_t currentBaud;
    static ChannelStatus channels[16];
    static uint8_t lastActiveChannel;
    static uint8_t runningStatus;
    static uint8_t msgBuffer[3];
    static uint8_t msgIndex;
    static uint8_t expectedLength;
    static uint16_t channelRPN[16];
    static uint16_t channelNRPN[16];

    static void processCompleteMessage();
};
