#pragma once

#include <Arduino.h>

class AudioEngine {
public:
    static bool begin();
    static bool loadSoundFont(const char* path);
    static void loadSoundFontAsync(const char* path); // 비동기 백그라운드 로드
    static bool isLoadingFont();                      // 로딩 진행 중 여부
    static const char* getCurrentFontName();
    
    // MIDI 이벤트 인터페이스 (개별 호출용 - 자체 락 획득)
    static void noteOn(uint8_t channel, uint8_t key, uint8_t velocity);
    static void noteOff(uint8_t channel, uint8_t key);
    static void programChange(uint8_t channel, uint8_t program);
    static void controlChange(uint8_t channel, uint8_t controller, uint8_t value);
    static void pitchBend(uint8_t channel, uint16_t value);
    static uint32_t getMidiQueueOverflowCount();
    // Must be called while holding getMutex(), before direct MIDI dispatch.
    static void processQueuedMidiEventsLocked();
    static void panic(); // All Sound Off
    static void systemReset(); // GM / GS System Reset
    
    static void setChannelDrumMode(uint8_t channel, bool isDrum);
    static void setBank(uint8_t channel, uint16_t bank);
    static void applyMT32ModeDirect();
    static void applyGMModeDirect();
    static void applyGSModeDirect();
    static void resetMT32FilterDirect();
    
    // Roland GS Reverb / Chorus Macro 및 Drum Key Parameter
    static void setReverbMacroDirect(uint8_t macroType);
    static void setChorusMacroDirect(uint8_t macroType);
    static void setGSReverbParamsDirect(uint8_t character, uint8_t level, uint8_t time, uint8_t fb);
    static void setGSChorusParamsDirect(uint8_t level, uint8_t fb, uint8_t delay, uint8_t rate, uint8_t depth);
    static void setMT32ReverbDirect(uint8_t mode, uint8_t time, uint8_t level);
    static void setDrumKeyPitchDirect(uint8_t key, int8_t pitch);
    static void setDrumKeyCutoffDirect(uint8_t key, int8_t cutoff);
    static void setDrumKeyLevelDirect(uint8_t key, uint8_t level);
    static void setDrumKeyPanDirect(uint8_t key, uint8_t pan);
    static void resetDrumKeyParamsDirect();
    static void setGSMasterEQDirect(uint8_t lowFreq, int8_t lowGain, uint8_t highFreq, int8_t highGain);
    static void setGSMasterEQ(uint8_t lowFreq, int8_t lowGain, uint8_t highFreq, int8_t highGain);
    static void setChannelKeyShiftDirect(uint8_t channel, int8_t semitones);
    static void setChannelKeyShift(uint8_t channel, int8_t semitones);
    static void setChannelTuningOffsetDirect(uint8_t channel, float semitones);
    static void setChannelTuningOffset(uint8_t channel, float semitones);
    static void setScaleTuningDirect(uint8_t channel, const int8_t* scale12);
    static void setScaleTuning(uint8_t channel, const int8_t* scale12);
    static void setChannelMonoDirect(uint8_t channel, bool isMono);
    static void setChannelMono(uint8_t channel, bool isMono);
    static void setPitchRangeDirect(uint8_t channel, float semitones);
    static void setPitchRange(uint8_t channel, float semitones);
    
    // 시퀀서 초고속 배치 디스패치 인터페이스 (호출자가 락 1회 획득 후 일괄 호출)
    static SemaphoreHandle_t getMutex();
    static void noteOnDirect(uint8_t channel, uint8_t key, uint8_t velocity);
    static void noteOffDirect(uint8_t channel, uint8_t key);
    static void programChangeDirect(uint8_t channel, uint8_t program);
    static void controlChangeDirect(uint8_t channel, uint8_t controller, uint8_t value);
    static void setBankDirect(uint8_t channel, uint16_t bank);
    static void pitchBendDirect(uint8_t channel, uint16_t value);
    static void setChannelDrumModeDirect(uint8_t channel, bool isDrum);
    static void panicDirect();
    static void systemResetDirect();
    static void setMasterVolumeDirect(uint8_t volume);

    // 마스터 볼륨 (0 ~ 100)
    static void setMasterVolume(uint8_t volume);
    static uint8_t getMasterVolume();

    // 오디오 모드 (스테레오 / 모노 다운믹스)
    static void setMonoMode(bool isMono);
    static bool isMonoMode();
    static void setHardwareMonoDetected(bool detected);
    static bool isHardwareMonoDetected();
    static bool isEffectiveMono(); // 웹 설정 또는 하드웨어 감지에 의해 실제 모노 여부

    // 활성 보이스 수 조회
    static int getActiveVoiceCount();

    // 원클릭 오디오 테스트 (1: Piano, 2: Guitar, 3: Drum, 4: Stereo 3D)
    static void playTestSound(int type);

    // NVS 볼륨 디바운스 플러시 (loop에서 호출)
    static void flushVolumeNVS();

private:
    static void audioTask(void* parameter);
    static bool initI2S();
    static volatile bool fontLoading;
    static volatile bool volumeNVSDirty;
    static unsigned long lastVolumeChangeTime;
};
