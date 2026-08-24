#pragma once

#include <Arduino.h>
#include <stdint.h>

// ==============================================================================
// 8-Band Parametric EQ (Direct Form II Transposed Biquad Cascade)
// Tailored for External Docking Speaker Acoustic Compensation (44.1kHz)
// 1. HPF 85Hz (Protects small driver from sub-bass excursion & distortion)
// 2. 210Hz +4.0dB Q=1.5 (Punchy bass enhancement)
// 3. 430Hz -7.0dB Q=1.5 (Boxy enclosure resonance reduction)
// 4. 1500Hz +1.5dB Q=1.0 (Presence / mid clarity)
// 5. 1690Hz -2.0dB Q=6.0 (Narrow peak notch)
// 6. 2100Hz +1.5dB Q=2.0 (Upper mid bite)
// 7. 2500Hz +3.0dB Q=2.0 (Lead & vocal clarity boost)
// 8. 5700Hz -3.0dB Q=0.7 (Harsh high-frequency taming)
// ==============================================================================

struct BiquadSection {
    float b0, b1, b2;
    float a1, a2;
    float z1, z2;
};

class SpeakerEQ {
public:
    SpeakerEQ() {
        reset();
    }

    void reset() {
        // Pre-calculated Biquad coefficients for 44.1kHz sampling rate
        sections[0] = { 0.99147318f, -1.98294636f, 0.99147318f, -1.98287365f, 0.98301907f, 0.0f, 0.0f }; // HPF 85Hz
        sections[1] = { 1.00459647f, -1.98339459f, 0.97968622f, -1.98339459f, 0.98428270f, 0.0f, 0.0f }; // PK 210Hz +4dB Q1.5
        sections[2] = { 0.98360441f, -1.93709603f, 0.95713261f, -1.93709603f, 0.94073702f, 0.0f, 0.0f }; // PK 430Hz -7dB Q1.5
        sections[3] = { 1.01671065f, -1.78123430f, 0.80599010f, -1.78123430f, 0.82270075f, 0.0f, 0.0f }; // PK 1500Hz +1.5dB Q1
        sections[4] = { 0.99551420f, -1.89994000f, 0.96086485f, -1.89994000f, 0.95637905f, 0.0f, 0.0f }; // PK 1690Hz -2dB Q6
        sections[5] = { 1.01193472f, -1.79014445f, 0.86143845f, -1.79014445f, 0.87337317f, 0.0f, 0.0f }; // PK 2100Hz +1.5dB Q2
        sections[6] = { 1.02819169f, -1.74636884f, 0.83513377f, -1.74636884f, 0.86332546f, 0.0f, 0.0f }; // PK 2500Hz +3dB Q2
        sections[7] = { 0.88866074f, -0.85138925f, 0.34888314f, -0.85138925f, 0.23754388f, 0.0f, 0.0f }; // PK 5700Hz -3dB Q0.7
    }

    // Direct Form II Transposed Biquad Filter Processing (32-bit Float 언롤링 & FPU 레지스터 최적화)
    inline void process(float* buffer, int numFrames) {
        processDownmixAndFilter(buffer, numFrames);
    }

    inline void processDownmixAndFilter(float* buffer, int numFrames) {
        if (!buffer || numFrames <= 0) return;

        // 8개 필터 계수 및 딜레이 상태 로컬 레지스터 캐싱 (메모리 왕복 0-Cycle)
        const float b0_0 = sections[0].b0, b1_0 = sections[0].b1, b2_0 = sections[0].b2, a1_0 = sections[0].a1, a2_0 = sections[0].a2;
        float z1_0 = sections[0].z1, z2_0 = sections[0].z2;

        const float b0_1 = sections[1].b0, b1_1 = sections[1].b1, b2_1 = sections[1].b2, a1_1 = sections[1].a1, a2_1 = sections[1].a2;
        float z1_1 = sections[1].z1, z2_1 = sections[1].z2;

        const float b0_2 = sections[2].b0, b1_2 = sections[2].b1, b2_2 = sections[2].b2, a1_2 = sections[2].a1, a2_2 = sections[2].a2;
        float z1_2 = sections[2].z1, z2_2 = sections[2].z2;

        const float b0_3 = sections[3].b0, b1_3 = sections[3].b1, b2_3 = sections[3].b2, a1_3 = sections[3].a1, a2_3 = sections[3].a2;
        float z1_3 = sections[3].z1, z2_3 = sections[3].z2;

        const float b0_4 = sections[4].b0, b1_4 = sections[4].b1, b2_4 = sections[4].b2, a1_4 = sections[4].a1, a2_4 = sections[4].a2;
        float z1_4 = sections[4].z1, z2_4 = sections[4].z2;

        const float b0_5 = sections[5].b0, b1_5 = sections[5].b1, b2_5 = sections[5].b2, a1_5 = sections[5].a1, a2_5 = sections[5].a2;
        float z1_5 = sections[5].z1, z2_5 = sections[5].z2;

        const float b0_6 = sections[6].b0, b1_6 = sections[6].b1, b2_6 = sections[6].b2, a1_6 = sections[6].a1, a2_6 = sections[6].a2;
        float z1_6 = sections[6].z1, z2_6 = sections[6].z2;

        const float b0_7 = sections[7].b0, b1_7 = sections[7].b1, b2_7 = sections[7].b2, a1_7 = sections[7].a1, a2_7 = sections[7].a2;
        float z1_7 = sections[7].z1, z2_7 = sections[7].z2;

        float* p = buffer;
        for (int i = 0; i < numFrames; i++) {
            // 1-Pass: 스테레오 모노 다운믹스 & 8밴드 PEQ 필터링 병합 (무손실 32-bit Float)
            float s = (p[0] + p[1]) * 0.5f;

            // 8-Band Unrolled Cascade
            float out0 = s * b0_0 + z1_0;
            z1_0 = s * b1_0 + z2_0 - a1_0 * out0;
            z2_0 = s * b2_0 - a2_0 * out0;

            float out1 = out0 * b0_1 + z1_1;
            z1_1 = out0 * b1_1 + z2_1 - a1_1 * out1;
            z2_1 = out0 * b2_1 - a2_1 * out1;

            float out2 = out1 * b0_2 + z1_2;
            z1_2 = out1 * b1_2 + z2_2 - a1_2 * out2;
            z2_2 = out1 * b2_2 - a2_2 * out2;

            float out3 = out2 * b0_3 + z1_3;
            z1_3 = out2 * b1_3 + z2_3 - a1_3 * out3;
            z2_3 = out2 * b2_3 - a2_3 * out3;

            float out4 = out3 * b0_4 + z1_4;
            z1_4 = out3 * b1_4 + z2_4 - a1_4 * out4;
            z2_4 = out3 * b2_4 - a2_4 * out4;

            float out5 = out4 * b0_5 + z1_5;
            z1_5 = out4 * b1_5 + z2_5 - a1_5 * out5;
            z2_5 = out4 * b2_5 - a2_5 * out5;

            float out6 = out5 * b0_6 + z1_6;
            z1_6 = out5 * b1_6 + z2_6 - a1_6 * out6;
            z2_6 = out5 * b2_6 - a2_6 * out6;

            float out7 = out6 * b0_7 + z1_7;
            z1_7 = out6 * b1_7 + z2_7 - a1_7 * out7;
            z2_7 = out6 * b2_7 - a2_7 * out7;

            p[0] = out7; // L
            p[1] = out7; // R
            p += 2;
        }

        // 상태값 동기화
        sections[0].z1 = z1_0; sections[0].z2 = z2_0;
        sections[1].z1 = z1_1; sections[1].z2 = z2_1;
        sections[2].z1 = z1_2; sections[2].z2 = z2_2;
        sections[3].z1 = z1_3; sections[3].z2 = z2_3;
        sections[4].z1 = z1_4; sections[4].z2 = z2_4;
        sections[5].z1 = z1_5; sections[5].z2 = z2_5;
        sections[6].z1 = z1_6; sections[6].z2 = z2_6;
        sections[7].z1 = z1_7; sections[7].z2 = z2_7;
    }

private:
    BiquadSection sections[8];
};
