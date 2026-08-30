#pragma once
#include <Arduino.h>
#include "mt32_pcm_data.h"

// Roland MT-32 LA-32 Synthesis Architecture Constants (4보이스 시스템 부하 최적화)
#define LA32_MAX_VOICES 4
#define LA32_SAMPLE_RATE 44100.0f
#define MT32_NATIVE_RATE 32000.0f

// 0-Division 초고속 인라인 안티에일리어싱 PolyBLEP (윈도우 가드 & 1사이클 곱셈 기반)
inline float fastPolyBLEP(float t, float invDt) {
    if (t < 0.5f) {
        float x = t * invDt;
        if (x < 1.0f) return x + x - x * x - 1.0f;
    } else {
        float x2 = (t - 1.0f) * invDt;
        if (x2 > -1.0f) return x2 * x2 + x2 + x2 + 1.0f;
    }
    return 0.0f;
}

struct LA32TimbreParam {
    char name[11];
    uint8_t structure12;
    uint8_t structure34;
    struct Partial {
        uint8_t wg_wave;
        uint8_t wg_pcmNum;
        uint8_t wg_pulseWidth;
        uint8_t tvf_cutoff;
        uint8_t tvf_reso;
        uint8_t tvf_envTime[5];
        uint8_t tvf_envLevel[5];
        uint8_t tva_level;
        uint8_t tva_velo;
        uint8_t tva_envTime[5];
        uint8_t tva_envLevel[5];
    } partials[4];
};

struct LA32Voice {
    bool active;
    uint32_t noteOnTimestamp;
    uint8_t channel;
    uint8_t key;
    uint8_t velocity;
    float panL;
    float panR;

    bool partActive[4];
    bool usePCM[4];
    uint32_t pcmOffset[4];
    uint32_t pcmLength[4];
    bool pcmLooped[4];
    float pcmPos[4];
    float pcmStep[4];

    bool isSaw[4];
    float oscPhase[4];
    float phaseInc[4];
    float invPhaseInc[4]; // 1.0f / phaseInc (루프 내 나눗셈 제거용 사전 계산)
    float pulseWidth[4];

    // TVF Low-Pass with Resonance
    float filterState[4];
    float filterAlpha[4];
    float filterReso[4];

    // 5-Stage TVA Envelope
    uint8_t envStage[4];      // 0..4
    uint32_t envCounter[4];
    uint32_t envDuration[4];
    float envLevel[4];
    float envTarget[4];
    float envInc[4];
    float velNorm;

    uint8_t structure12;
    uint8_t structure34;
    float pitchSlide;
    float pitchSlideStep;
    float pitchBendSemitones;
};

class LA32SynthEngine {
public:
    static void init();
    static void reset();

    static void noteOn(uint8_t channel, uint8_t key, uint8_t velocity, float panNorm = 0.5f);
    static void noteOff(uint8_t channel, uint8_t key);
    static void allNotesOff(uint8_t channel);
    static void pitchBend(uint8_t channel, float semitones);

    static void setCustomTimbre(uint8_t channel, const uint8_t* timbreData, size_t length);
    static bool isChannelCustom(uint8_t channel);
    static const char* getCustomTimbreName(uint8_t channel);
    static void clearCustomChannel(uint8_t channel);

    static inline int getActiveVoiceCount() { return activeVoiceCount; }

    static inline void render(float* buffer, int numFrames) {
        if (activeVoiceCount == 0 || !buffer || numFrames <= 0) return;
        renderVoices(buffer, numFrames);
    }

private:
    static LA32Voice voices[LA32_MAX_VOICES];
    static LA32TimbreParam channelTimbres[16];
    static bool channelIsCustom[16];
    static volatile int activeVoiceCount;

    static void renderVoices(float* buffer, int numFrames);
};
