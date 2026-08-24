#pragma once

#include <Arduino.h>
#include <string.h>

class StereoReverb {
private:
    // Comb Filter Buffer Delays for 44.1kHz
    static const int COMB_L_1 = 1116, COMB_L_2 = 1188, COMB_L_3 = 1277, COMB_L_4 = 1356;
    static const int COMB_R_1 = 1139, COMB_R_2 = 1211, COMB_R_3 = 1300, COMB_R_4 = 1379;
    static const int ALLPASS_1 = 556, ALLPASS_2 = 441;
    static const int PREDELAY_LEN = 661; // 15ms Pre-delay at 44.1kHz

    float buf_cL1[COMB_L_1], buf_cL2[COMB_L_2], buf_cL3[COMB_L_3], buf_cL4[COMB_L_4];
    float buf_cR1[COMB_R_1], buf_cR2[COMB_R_2], buf_cR3[COMB_R_3], buf_cR4[COMB_R_4];
    float buf_apL1[ALLPASS_1], buf_apL2[ALLPASS_2];
    float buf_apR1[ALLPASS_1], buf_apR2[ALLPASS_2];
    float buf_preL[PREDELAY_LEN], buf_preR[PREDELAY_LEN];

    int idx_cL1, idx_cL2, idx_cL3, idx_cL4;
    int idx_cR1, idx_cR2, idx_cR3, idx_cR4;
    int idx_apL1, idx_apL2;
    int idx_apR1, idx_apR2;
    int idx_pre;

    float store_cL1, store_cL2, store_cL3, store_cL4;
    float store_cR1, store_cR2, store_cR3, store_cR4;

    float damp1, damp2, feedback, wetGain, dryGain;
    float hpfPrevInL, hpfPrevOutL;
    float hpfPrevInR, hpfPrevOutR;

    inline float processComb(float in, float* buf, int size, int& idx, float& store) {
        float out = buf[idx];
        store = (out * damp2) + (store * damp1);
        buf[idx] = in + (store * feedback) + 1.0e-15f; // Anti-denormal offset (분기문 제거 및 0-cycle 방지)
        if (++idx >= size) idx = 0;
        return out;
    }

    inline float processAllpass(float in, float* buf, int size, int& idx) {
        float bufout = buf[idx];
        float out = -in + bufout;
        buf[idx] = in + (bufout * 0.5f) + 1.0e-15f;
        if (++idx >= size) idx = 0;
        return out;
    }

public:
    void reset() {
        memset(buf_cL1, 0, sizeof(buf_cL1)); memset(buf_cL2, 0, sizeof(buf_cL2));
        memset(buf_cL3, 0, sizeof(buf_cL3)); memset(buf_cL4, 0, sizeof(buf_cL4));
        memset(buf_cR1, 0, sizeof(buf_cR1)); memset(buf_cR2, 0, sizeof(buf_cR2));
        memset(buf_cR3, 0, sizeof(buf_cR3)); memset(buf_cR4, 0, sizeof(buf_cR4));
        memset(buf_apL1, 0, sizeof(buf_apL1)); memset(buf_apL2, 0, sizeof(buf_apL2));
        memset(buf_apR1, 0, sizeof(buf_apR1)); memset(buf_apR2, 0, sizeof(buf_apR2));
        memset(buf_preL, 0, sizeof(buf_preL)); memset(buf_preR, 0, sizeof(buf_preR));

        idx_cL1 = idx_cL2 = idx_cL3 = idx_cL4 = 0;
        idx_cR1 = idx_cR2 = idx_cR3 = idx_cR4 = 0;
        idx_apL1 = idx_apL2 = idx_apR1 = idx_apR2 = 0;
        idx_pre = 0;

        store_cL1 = store_cL2 = store_cL3 = store_cL4 = 0.0f;
        store_cR1 = store_cR2 = store_cR3 = store_cR4 = 0.0f;

        hpfPrevInL = hpfPrevOutL = 0.0f;
        hpfPrevInR = hpfPrevOutR = 0.0f;
    }

    StereoReverb() {
        reset();
        damp1 = 0.25f;
        damp2 = 0.75f;
        feedback = 0.72f;
        wetGain = 0.16f; // 깔끔하고 자연스러운 스튜디오 룸 공간감
        dryGain = 0.88f; // 정규화된 헤드룸
    }

    // Roland GS Reverb Macro 8종 실시간 알고리즘 스와핑
    void setMacro(uint8_t macroType) {
        switch (macroType & 0x07) {
            case 0: // Room 1
                feedback = 0.55f; damp1 = 0.40f; damp2 = 0.60f; wetGain = 0.12f; dryGain = 0.90f; break;
            case 1: // Room 2
                feedback = 0.65f; damp1 = 0.35f; damp2 = 0.65f; wetGain = 0.14f; dryGain = 0.88f; break;
            case 2: // Room 3 (기본값)
                feedback = 0.72f; damp1 = 0.25f; damp2 = 0.75f; wetGain = 0.16f; dryGain = 0.88f; break;
            case 3: // Hall 1
                feedback = 0.80f; damp1 = 0.20f; damp2 = 0.80f; wetGain = 0.18f; dryGain = 0.85f; break;
            case 4: // Hall 2 (넓은 공연장 잔향)
                feedback = 0.86f; damp1 = 0.15f; damp2 = 0.85f; wetGain = 0.22f; dryGain = 0.82f; break;
            case 5: // Plate
                feedback = 0.78f; damp1 = 0.08f; damp2 = 0.92f; wetGain = 0.20f; dryGain = 0.85f; break;
            case 6: // Delay
                feedback = 0.70f; damp1 = 0.10f; damp2 = 0.90f; wetGain = 0.16f; dryGain = 0.88f; break;
            case 7: // Panning Delay
                feedback = 0.75f; damp1 = 0.10f; damp2 = 0.90f; wetGain = 0.18f; dryGain = 0.85f; break;
        }
    }

    void setGSParameters(uint8_t character, uint8_t level, uint8_t time, uint8_t fb) {
        if (time > 0 && time <= 127) {
            feedback = 0.40f + ((float)time / 127.0f) * 0.48f; // 0.40 ~ 0.88
        }
        if (fb <= 127) {
            damp1 = 0.10f + (1.0f - (float)fb / 127.0f) * 0.35f;
            damp2 = 1.0f - damp1;
        }
        if (level <= 127) {
            wetGain = ((float)level / 127.0f) * 0.28f;
        }
    }

    // Roland MT-32 Boss DSP 리버브 모드 (0:Room, 1:Hall, 2:Plate, 3:TapDelay) 및 타임/레벨 실시간 튜닝
    void setMT32Profile(uint8_t mode, uint8_t time, uint8_t level) {
        float timeScale = (time > 0 && time <= 8) ? ((float)time / 5.0f) : 1.0f;
        float wetScale = ((float)level / 64.0f);
        if (wetScale > 1.5f) wetScale = 1.5f;

        switch (mode & 0x03) {
            case 0: // Room: 실기 특유의 따뜻하고 오밀조밀한 챔버 리버브
                feedback = 0.65f * timeScale; if (feedback > 0.85f) feedback = 0.85f;
                damp1 = 0.35f; damp2 = 0.65f;
                wetGain = 0.18f * wetScale; dryGain = 0.86f;
                break;
            case 1: // Hall (MT-32 기본값): 실기 Boss DSP의 깊고 웅장한 아날로그 홀 울림
                feedback = 0.82f * timeScale; if (feedback > 0.92f) feedback = 0.92f;
                damp1 = 0.22f; damp2 = 0.78f;
                wetGain = 0.26f * wetScale; dryGain = 0.80f;
                break;
            case 2: // Plate: 고역이 찰랑거리는 메탈 플레이트 울림
                feedback = 0.76f * timeScale; if (feedback > 0.88f) feedback = 0.88f;
                damp1 = 0.10f; damp2 = 0.90f;
                wetGain = 0.22f * wetScale; dryGain = 0.84f;
                break;
            case 3: // Tap Delay: 패닝 딜레이
                feedback = 0.70f * timeScale; if (feedback > 0.85f) feedback = 0.85f;
                damp1 = 0.15f; damp2 = 0.85f;
                wetGain = 0.20f * wetScale; dryGain = 0.85f;
                break;
        }
    }

    void process(float* buffer, int numFrames) {
        if (!buffer || numFrames <= 0) return;

        const float dGain = dryGain;
        const float wGain = wetGain;

        float* ptr = buffer;
        for (int i = 0; i < numFrames; i++) {
            float inL = ptr[0];
            float inR = ptr[1];

            // True Stereo 80Hz High-Pass Filter (서브우퍼 저역 컷 + 따뜻한 저음 룸 잔향 보존)
            float hpfInL = 0.9887f * (hpfPrevOutL + inL - hpfPrevInL);
            float hpfInR = 0.9887f * (hpfPrevOutR + inR - hpfPrevInR);
            hpfPrevInL = inL;
            hpfPrevInR = inR;
            hpfPrevOutL = hpfInL;
            hpfPrevOutR = hpfInR;

            // 15ms Pre-Delay Buffer
            float preL = buf_preL[idx_pre];
            float preR = buf_preR[idx_pre];
            buf_preL[idx_pre] = hpfInL;
            buf_preR[idx_pre] = hpfInR;
            if (++idx_pre >= PREDELAY_LEN) idx_pre = 0;

            // Left & Right Comb Filters (파이프라이닝)
            float outL = processComb(preL, buf_cL1, COMB_L_1, idx_cL1, store_cL1) +
                         processComb(preL, buf_cL2, COMB_L_2, idx_cL2, store_cL2) +
                         processComb(preL, buf_cL3, COMB_L_3, idx_cL3, store_cL3) +
                         processComb(preL, buf_cL4, COMB_L_4, idx_cL4, store_cL4);

            float outR = processComb(preR, buf_cR1, COMB_R_1, idx_cR1, store_cR1) +
                         processComb(preR, buf_cR2, COMB_R_2, idx_cR2, store_cR2) +
                         processComb(preR, buf_cR3, COMB_R_3, idx_cR3, store_cR3) +
                         processComb(preR, buf_cR4, COMB_R_4, idx_cR4, store_cR4);

            // Allpass Filters
            outL = processAllpass(outL * 0.25f, buf_apL1, ALLPASS_1, idx_apL1);
            outR = processAllpass(outR * 0.25f, buf_apR1, ALLPASS_1, idx_apR1);

            outL = processAllpass(outL, buf_apL2, ALLPASS_2, idx_apL2);
            outR = processAllpass(outR, buf_apR2, ALLPASS_2, idx_apR2);

            // Wet / Dry Mix (무손실 32-bit Float)
            ptr[0] = inL * dGain + outL * wGain;
            ptr[1] = inR * dGain + outR * wGain;
            ptr += 2;
        }
    }
};
