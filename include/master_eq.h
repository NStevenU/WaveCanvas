#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <math.h>

// ==============================================================================
// Roland GS 2-Band Master Parametric EQ (Low-Shelf & High-Shelf Cascade)
// - Low Shelf: 200Hz / 400Hz, Gain: -12dB ~ +12dB (1dB step)
// - High Shelf: 3.0kHz / 6.0kHz, Gain: -12dB ~ +12dB (1dB step)
// - ESP32-S3 Direct Form II Transposed Biquad (Super-fast ~12us on FPU)
// - Auto-bypass when both gains are 0dB (Zero CPU overhead)
// ==============================================================================

struct MasterEQSection {
    float b0, b1, b2;
    float a1, a2;
    float z1_L, z2_L;
    float z1_R, z2_R;
};

class MasterEQ {
public:
    MasterEQ() {
        reset();
    }

    void reset() {
        lowFreqType = 0;   // 200Hz
        lowGainDb = 0;     // 0dB
        highFreqType = 0;  // 3.0kHz
        highGainDb = 0;    // 0dB
        bypassed = true;
        recalculateCoefficients();
        clearHistory();
    }

    void clearHistory() {
        for (int i = 0; i < 2; i++) {
            sections[i].z1_L = sections[i].z2_L = 0.0f;
            sections[i].z1_R = sections[i].z2_R = 0.0f;
        }
    }

    void setParameters(uint8_t lowFreq, int8_t lowGain, uint8_t highFreq, int8_t highGain) {
        lowFreqType = (lowFreq != 0) ? 1 : 0;
        lowGainDb = constrain(lowGain, -12, 12);
        highFreqType = (highFreq != 0) ? 1 : 0;
        highGainDb = constrain(highGain, -12, 12);
        bypassed = (lowGainDb == 0 && highGainDb == 0);
        recalculateCoefficients();
    }

    inline void process(float* buffer, int numFrames) {
        if (bypassed || !buffer || numFrames <= 0) return;

        // 계수 로컬 캐싱 (메모리 로드 오버헤드 0화 및 FPU 레지스터 고정)
        const float b0_0 = sections[0].b0, b1_0 = sections[0].b1, b2_0 = sections[0].b2;
        const float a1_0 = sections[0].a1, a2_0 = sections[0].a2;
        float z1_L0 = sections[0].z1_L, z2_L0 = sections[0].z2_L;
        float z1_R0 = sections[0].z1_R, z2_R0 = sections[0].z2_R;

        const float b0_1 = sections[1].b0, b1_1 = sections[1].b1, b2_1 = sections[1].b2;
        const float a1_1 = sections[1].a1, a2_1 = sections[1].a2;
        float z1_L1 = sections[1].z1_L, z2_L1 = sections[1].z2_L;
        float z1_R1 = sections[1].z1_R, z2_R1 = sections[1].z2_R;

        float* p = buffer;
        for (int i = 0; i < numFrames; i++) {
            float sL = p[0];
            float sR = p[1];

            // Section 0 (Low Shelf): L/R 듀얼 FPU 파이프라이닝
            float outL0 = sL * b0_0 + z1_L0;
            float outR0 = sR * b0_0 + z1_R0;
            z1_L0 = sL * b1_0 + z2_L0 - a1_0 * outL0;
            z1_R0 = sR * b1_0 + z2_R0 - a1_0 * outR0;
            z2_L0 = sL * b2_0 - a2_0 * outL0;
            z2_R0 = sR * b2_0 - a2_0 * outR0;

            // Section 1 (High Shelf): L/R 듀얼 FPU 파이프라이닝 (무손실 32-bit Float, 하드 클리핑 100% 제거)
            float outL1 = outL0 * b0_1 + z1_L1;
            float outR1 = outR0 * b0_1 + z1_R1;
            z1_L1 = outL0 * b1_1 + z2_L1 - a1_1 * outL1;
            z1_R1 = outR0 * b1_1 + z2_R1 - a1_1 * outR1;
            z2_L1 = outL0 * b2_1 - a2_1 * outL1;
            z2_R1 = outR0 * b2_1 - a2_1 * outR1;

            p[0] = outL1;
            p[1] = outR1;
            p += 2;
        }

        // 상태값 동기화
        sections[0].z1_L = z1_L0; sections[0].z2_L = z2_L0;
        sections[0].z1_R = z1_R0; sections[0].z2_R = z2_R0;
        sections[1].z1_L = z1_L1; sections[1].z2_L = z2_L1;
        sections[1].z1_R = z1_R1; sections[1].z2_R = z2_R1;
    }

private:
    uint8_t lowFreqType;
    int8_t  lowGainDb;
    uint8_t highFreqType;
    int8_t  highGainDb;
    bool    bypassed;
    MasterEQSection sections[2];

    void calculateLowShelf(float f0, float gainDb, MasterEQSection& sec) {
        if (gainDb == 0.0f) {
            sec.b0 = 1.0f; sec.b1 = 0.0f; sec.b2 = 0.0f;
            sec.a1 = 0.0f; sec.a2 = 0.0f;
            return;
        }
        float Fs = 44100.0f;
        float A = powf(10.0f, gainDb / 40.0f);
        float w0 = 2.0f * (float)M_PI * f0 / Fs;
        float cosw0 = cosf(w0);
        float sinw0 = sinf(w0);
        float S = 1.0f; // shelf slope
        float alpha = sinw0 / 2.0f * sqrtf((A + 1.0f / A) * (1.0f / S - 1.0f) + 2.0f);
        float two_sqrtA_alpha = 2.0f * sqrtf(A) * alpha;

        float a0 = (A + 1.0f) + (A - 1.0f) * cosw0 + two_sqrtA_alpha;
        sec.b0 = (A * ((A + 1.0f) - (A - 1.0f) * cosw0 + two_sqrtA_alpha)) / a0;
        sec.b1 = (2.0f * A * ((A - 1.0f) - (A + 1.0f) * cosw0)) / a0;
        sec.b2 = (A * ((A + 1.0f) - (A - 1.0f) * cosw0 - two_sqrtA_alpha)) / a0;
        sec.a1 = (-2.0f * ((A - 1.0f) + (A + 1.0f) * cosw0)) / a0;
        sec.a2 = ((A + 1.0f) + (A - 1.0f) * cosw0 - two_sqrtA_alpha) / a0;
    }

    void calculateHighShelf(float f0, float gainDb, MasterEQSection& sec) {
        if (gainDb == 0.0f) {
            sec.b0 = 1.0f; sec.b1 = 0.0f; sec.b2 = 0.0f;
            sec.a1 = 0.0f; sec.a2 = 0.0f;
            return;
        }
        float Fs = 44100.0f;
        float A = powf(10.0f, gainDb / 40.0f);
        float w0 = 2.0f * (float)M_PI * f0 / Fs;
        float cosw0 = cosf(w0);
        float sinw0 = sinf(w0);
        float S = 1.0f;
        float alpha = sinw0 / 2.0f * sqrtf((A + 1.0f / A) * (1.0f / S - 1.0f) + 2.0f);
        float two_sqrtA_alpha = 2.0f * sqrtf(A) * alpha;

        float a0 = (A + 1.0f) - (A - 1.0f) * cosw0 + two_sqrtA_alpha;
        sec.b0 = (A * ((A + 1.0f) + (A - 1.0f) * cosw0 + two_sqrtA_alpha)) / a0;
        sec.b1 = (-2.0f * A * ((A - 1.0f) + (A + 1.0f) * cosw0)) / a0;
        sec.b2 = (A * ((A + 1.0f) + (A - 1.0f) * cosw0 - two_sqrtA_alpha)) / a0;
        sec.a1 = (2.0f * ((A - 1.0f) - (A + 1.0f) * cosw0)) / a0;
        sec.a2 = ((A + 1.0f) - (A - 1.0f) * cosw0 - two_sqrtA_alpha) / a0;
    }

    void recalculateCoefficients() {
        float fLow = (lowFreqType == 0) ? 200.0f : 400.0f;
        float fHigh = (highFreqType == 0) ? 3000.0f : 6000.0f;

        calculateLowShelf(fLow, (float)lowGainDb, sections[0]);
        calculateHighShelf(fHigh, (float)highGainDb, sections[1]);
    }
};
