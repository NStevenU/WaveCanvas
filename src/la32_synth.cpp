#include "la32_synth.h"
#include "audio_engine.h"
#include "midi_parser.h"
#include "mt32_prog_data.h"
#include <math.h>

LA32Voice LA32SynthEngine::voices[LA32_MAX_VOICES];
LA32TimbreParam LA32SynthEngine::channelTimbres[16];
bool LA32SynthEngine::channelIsCustom[16] = {false};
volatile int LA32SynthEngine::activeVoiceCount = 0;

static float s_freq_table[128];
static bool s_freq_inited = false;

static void initFreqTable() {
    if (s_freq_inited) return;
    for (int i = 0; i < 128; i++) {
        s_freq_table[i] = 440.0f * powf(2.0f, (float)(i - 69) / 12.0f);
    }
    s_freq_inited = true;
}

void LA32SynthEngine::init() {
    initFreqTable();
    reset();
}

void LA32SynthEngine::reset() {
    for (int i = 0; i < LA32_MAX_VOICES; i++) {
        voices[i].active = false;
        for (int p = 0; p < 4; p++) {
            voices[i].partActive[p] = false;
        }
    }
    for (int ch = 0; ch < 16; ch++) {
        channelIsCustom[ch] = false;
        memset(&channelTimbres[ch], 0, sizeof(LA32TimbreParam));
    }
    activeVoiceCount = 0;
}

void LA32SynthEngine::clearCustomChannel(uint8_t channel) {
    if (channel < 16) {
        channelIsCustom[channel] = false;
        allNotesOff(channel);
    }
}

bool LA32SynthEngine::isChannelCustom(uint8_t channel) {
    return (channel < 16) && channelIsCustom[channel];
}

const char* LA32SynthEngine::getCustomTimbreName(uint8_t channel) {
    if (channel >= 16 || !channelIsCustom[channel]) return nullptr;
    return channelTimbres[channel].name;
}

void LA32SynthEngine::setCustomTimbre(uint8_t channel, const uint8_t* data, size_t length) {
    if (channel >= 16 || !data || length < 10) return;
    LA32TimbreParam& tp = channelTimbres[channel];
    memset(&tp, 0, sizeof(LA32TimbreParam));
    channelIsCustom[channel] = true;

    // Timbre Name (10 bytes)
    for (int i = 0; i < 10; i++) {
        tp.name[i] = (data[i] >= 32 && data[i] <= 126) ? (char)data[i] : ' ';
    }
    tp.name[10] = '\0';

    if (length >= 12) {
        tp.structure12 = (data[10] & 0x0F) + 1;
        tp.structure34 = (data[11] & 0x0F) + 1;
    }

    // Parse 4 Partials (Roland MT-32 Timbre Memory: Name 10B + Struct 2B = Offset 12)
    size_t offset = 12;
    for (int p = 0; p < 4; p++) {
        if (offset + 58 <= length) {
            const uint8_t* pd = &data[offset];
            tp.partials[p].wg_wave = pd[0] & 0x03; // 0: Square, 1: Sawtooth, 2: PCM
            tp.partials[p].wg_pcmNum = pd[1] & 0x7F;
            tp.partials[p].wg_pulseWidth = pd[2] & 0x7F;
            tp.partials[p].tvf_cutoff = pd[16] & 0x7F;
            tp.partials[p].tvf_reso = pd[17] & 0x1F;
            for (int i = 0; i < 5; i++) {
                tp.partials[p].tvf_envTime[i] = pd[18 + i] & 0x7F;
                tp.partials[p].tvf_envLevel[i] = pd[23 + i] & 0x7F;
            }
            tp.partials[p].tva_level = pd[42] & 0x7F;
            tp.partials[p].tva_velo = pd[43] & 0x7F;
            for (int i = 0; i < 5; i++) {
                tp.partials[p].tva_envTime[i] = pd[44 + i] & 0x7F;
                tp.partials[p].tva_envLevel[i] = pd[49 + i] & 0x7F;
            }
        } else {
            // 짧은 SysEx 수신 시 최적화된 아날로그 신스 리드 기본값 자동 구성 (Lead Saw + Pulse Sub 2개 파셜)
            if (p == 0) { // Main Singing Sawtooth
                tp.partials[0].wg_wave = 1;
                tp.partials[0].tva_level = 90;
                tp.partials[0].tvf_cutoff = 105;
            } else if (p == 1) { // Warm Pulse Sub
                tp.partials[1].wg_wave = 0;
                tp.partials[1].wg_pulseWidth = 50;
                tp.partials[1].tva_level = 70;
                tp.partials[1].tvf_cutoff = 95;
            } else {
                tp.partials[p].tva_level = 0;
            }
            for (int i = 0; i < 5; i++) {
                tp.partials[p].tva_envTime[i] = (i == 0) ? 5 : ((i == 4) ? 20 : 50);
                tp.partials[p].tva_envLevel[i] = (i == 3) ? 80 : ((i == 4) ? 0 : 100);
            }
        }
        offset += 58;
    }
}

void LA32SynthEngine::noteOn(uint8_t channel, uint8_t key, uint8_t velocity, float panNorm) {
    if (velocity == 0) {
        noteOff(channel, key);
        return;
    }
    if (key >= 128 || channel >= 16) return;

    static uint32_t s_voiceSeq = 0;
    s_voiceSeq++;

    // 1-Pass 단일 순회: 비활성 슬롯(1순위) > 릴리즈 슬롯(2순위) > 가장 오래된 LRU 슬롯(3순위) 즉시 선별
    int voiceIdx = -1;
    int relIdx = -1;
    uint32_t oldestRelAge = 0;
    int lruIdx = 0;
    uint32_t oldestLruAge = 0;

    for (int i = 0; i < LA32_MAX_VOICES; i++) {
        if (!voices[i].active) {
            voiceIdx = i;
            break;
        }
        uint32_t age = s_voiceSeq - voices[i].noteOnTimestamp;
        if (age >= oldestLruAge) {
            oldestLruAge = age;
            lruIdx = i;
        }
        bool inRelease = true;
        for (int p = 0; p < 4; p++) {
            if (voices[i].partActive[p] && voices[i].envStage[p] < 4) {
                inRelease = false;
                break;
            }
        }
        if (inRelease && age >= oldestRelAge) {
            oldestRelAge = age;
            relIdx = i;
        }
    }
    if (voiceIdx < 0) voiceIdx = (relIdx >= 0) ? relIdx : lruIdx;

    LA32Voice& v = voices[voiceIdx];
    bool isVoiceReused = v.active;
    v.active = true;
    v.noteOnTimestamp = s_voiceSeq;
    v.channel = channel;
    v.key = key;
    v.velocity = velocity;

    if (panNorm < 0.0f) panNorm = 0.0f;
    if (panNorm > 1.0f) panNorm = 1.0f;
    v.panL = cosf(panNorm * 1.5707963f);
    v.panR = sinf(panNorm * 1.5707963f);

    const LA32TimbreParam& tp = channelTimbres[channel];
    v.structure12 = tp.structure12 ? tp.structure12 : 1;
    v.structure34 = tp.structure34 ? tp.structure34 : 1;
    v.pitchSlide = 0.012f; // 어택 피치 슬라이드
    v.pitchSlideStep = v.pitchSlide / (float)(0.030f * LA32_SAMPLE_RATE);
    v.pitchBendSemitones = 0.0f;
    v.velNorm = (MIDIParser::getSynthMode() == SYNTH_MODE_MT32) ? MT32_VELO_LUT[velocity & 0x7F] : ((float)velocity / 127.0f);

    for (int p = 0; p < 4; p++) {
        const auto& part = tp.partials[p];
        if (part.tva_level == 0) {
            v.partActive[p] = false;
            continue;
        }
        v.partActive[p] = true;
        
        // Roland MT-32 정밀 규격: wg_wave가 2일 때만 PCM 롬 재생 (0: Square, 1: Sawtooth 합성 파형)
        v.usePCM[p] = (part.wg_wave == 2);

        float freq = s_freq_table[key & 0x7F];
        if (p == 2 && !v.usePCM[p]) {
            freq *= 1.0025f; // Part 3 디튠(Detune)으로 자연스러운 아날로그 코러스감 형성
        }

        if (v.usePCM[p]) {
            uint8_t pcmID = part.wg_pcmNum & 0x7F;
            const MT32PCMEntry& entry = MT32_PCM_TABLE[pcmID];
            v.pcmOffset[p] = entry.addr;
            v.pcmLength[p] = entry.len;
            v.pcmLooped[p] = entry.loop;
            v.pcmPos[p] = 0.0f;
            // Roland MT-32 PCM Attack ROM 음높이별 피치 스케일링
            float pitchDiff = ((float)key * 256.0f - ((float)entry.pitch - 5120.0f)) / 3072.0f;
            v.pcmStep[p] = powf(2.0f, pitchDiff) * (32000.0f / 44100.0f);
        } else {
            v.isSaw[p] = (part.wg_wave == 1);
            v.oscPhase[p] = 0.0f;
            v.phaseInc[p] = freq / LA32_SAMPLE_RATE;
            v.invPhaseInc[p] = (v.phaseInc[p] > 1e-6f) ? (1.0f / v.phaseInc[p]) : 0.0f;
            v.pulseWidth[p] = (float)part.wg_pulseWidth / 100.0f;
            if (v.pulseWidth[p] < 0.05f) v.pulseWidth[p] = 0.05f;
            if (v.pulseWidth[p] > 0.95f) v.pulseWidth[p] = 0.95f;

            // TVF 1-Pole Low-Pass 필터 계수 계산
            float cutoffNorm = (float)part.tvf_cutoff / 127.0f;
            v.filterAlpha[p] = cutoffNorm * cutoffNorm * 0.95f + 0.05f;
            v.filterState[p] = 0.0f;
        }

        // 보이스 재할당(Stealing) 시 파형 급단절 팝 노이즈(DC Click)를 방지하는 미세 소프트 램프
        if (!isVoiceReused || !v.partActive[p]) {
            v.envLevel[p] = 0.0f;
        }
        v.envStage[p] = 0; // Stage 0: Attack 1
        v.envCounter[p] = 0;
        float t1 = max(1.0f, (float)part.tva_envTime[0]);
        v.envDuration[p] = (uint32_t)(max(0.003f, t1 * 0.003f) * LA32_SAMPLE_RATE);
        float l1 = ((float)part.tva_envLevel[0] / 100.0f) * ((float)part.tva_level / 100.0f) * v.velNorm;
        v.envTarget[p] = l1;
        v.envInc[p] = (v.envTarget[p] - v.envLevel[p]) / (float)v.envDuration[p];
    }

    if (!isVoiceReused) {
        activeVoiceCount++;
    }
}

void LA32SynthEngine::noteOff(uint8_t channel, uint8_t key) {
    for (int i = 0; i < LA32_MAX_VOICES; i++) {
        if (voices[i].active && voices[i].channel == channel && voices[i].key == key) {
            const LA32TimbreParam& tp = channelTimbres[channel];
            for (int p = 0; p < 4; p++) {
                if (voices[i].partActive[p] && voices[i].envStage[p] < 4) {
                    voices[i].envStage[p] = 4; // Stage 4: Release (T5 / L5)
                    voices[i].envCounter[p] = 0;
                    float t5 = max(1.0f, (float)tp.partials[p].tva_envTime[4]);
                    voices[i].envDuration[p] = (uint32_t)(max(0.010f, t5 * 0.008f) * LA32_SAMPLE_RATE);
                    voices[i].envTarget[p] = 0.0f;
                    voices[i].envInc[p] = (0.0f - voices[i].envLevel[p]) / (float)voices[i].envDuration[p];
                }
            }
        }
    }
}

void LA32SynthEngine::allNotesOff(uint8_t channel) {
    for (int i = 0; i < LA32_MAX_VOICES; i++) {
        if (voices[i].active && voices[i].channel == channel) {
            voices[i].active = false;
            for (int p = 0; p < 4; p++) {
                voices[i].partActive[p] = false;
            }
        }
    }
    int count = 0;
    for (int i = 0; i < LA32_MAX_VOICES; i++) {
        if (voices[i].active) count++;
    }
    activeVoiceCount = count;
}

void LA32SynthEngine::pitchBend(uint8_t channel, float semitones) {
    for (int i = 0; i < LA32_MAX_VOICES; i++) {
        LA32Voice& v = voices[i];
        if (v.active && v.channel == channel) {
            v.pitchBendSemitones = semitones;
            float bendMul = powf(2.0f, semitones / 12.0f);
            const LA32TimbreParam& tp = channelTimbres[channel];
            for (int p = 0; p < 4; p++) {
                if (!v.partActive[p]) continue;
                if (v.usePCM[p]) {
                    uint8_t pcmID = tp.partials[p].wg_pcmNum & 0x7F;
                    const MT32PCMEntry& entry = MT32_PCM_TABLE[pcmID];
                    float pitchDiff = ((float)v.key * 256.0f - ((float)entry.pitch - 5120.0f)) / 3072.0f;
                    v.pcmStep[p] = powf(2.0f, pitchDiff) * (32000.0f / 44100.0f) * bendMul;
                } else {
                    float freq = s_freq_table[v.key & 0x7F] * bendMul;
                    if (p == 2) freq *= 1.0025f;
                    v.phaseInc[p] = freq / LA32_SAMPLE_RATE;
                    v.invPhaseInc[p] = (v.phaseInc[p] > 1e-6f) ? (1.0f / v.phaseInc[p]) : 0.0f;
                }
            }
        }
    }
}

void LA32SynthEngine::renderVoices(float* buffer, int numFrames) {
    if (numFrames > 512) numFrames = 512;
    int activeCount = 0;

    // TSF 사운드폰트 엔진과 1:1 완벽하게 일치하는 마스터 볼륨 정규화 (0.80f Gervill 기준 + -6dB 헤드룸 보정)
    float normVol = (float)AudioEngine::getMasterVolume() / 100.0f;
    float masterGain = normVol * normVol * 0.80f * 0.50f;

    // 512샘플 전용 static float 어큐뮬레이터 버퍼 (BSS 정적 메모리 배치로 Task 스택 고갈 100% 방지)
    static float mixAccL[512];
    static float mixAccR[512];
    memset(mixAccL, 0, numFrames * sizeof(float));
    memset(mixAccR, 0, numFrames * sizeof(float));

    // 실시간 활성 보이스 수 사전 카운트 (동적 헤드룸 0ms 즉시 보정)
    int curVoices = 0;
    for (int i = 0; i < LA32_MAX_VOICES; i++) {
        if (voices[i].active) curVoices++;
    }
    float polyComp = 1.0f;
    if (curVoices > 2) {
        polyComp = 1.0f / sqrtf(1.0f + 0.35f * (float)(curVoices - 2));
    }

    for (int i = 0; i < LA32_MAX_VOICES; i++) {
        LA32Voice& v = voices[i];
        if (!v.active) continue;

        // [최적화 1] 활성 파셜 인덱스 압축 (비활성 파셜 루프 순회 100% 제거)
        int activeParts[4];
        int numActiveParts = 0;
        for (int p = 0; p < 4; p++) {
            if (v.partActive[p]) {
                activeParts[numActiveParts++] = p;
            }
        }
        if (numActiveParts == 0) {
            v.active = false;
            continue;
        }

        // 미디 채널 볼륨 (CC 7) 및 익스프레션 (CC 11) 연동
        const ChannelStatus& cs = MIDIParser::getChannelStatus(v.channel);
        float chVol = (float)cs.volume / 127.0f;
        float chExpr = (float)cs.expression / 127.0f;
        float voiceGain = masterGain * chVol * chExpr;

        // 동적 가변 게인 (Float 정규화):
        float normGain = voiceGain * 0.38f * polyComp;
        float scaleL = v.panL * normGain;
        float scaleR = v.panR * normGain;

        const LA32TimbreParam& tp = channelTimbres[v.channel];
        bool hasRingMod12 = (v.structure12 >= 5 && v.partActive[0] && v.partActive[1]);
        bool hasRingMod34 = (v.structure34 >= 5 && v.partActive[2] && v.partActive[3]);

        bool voiceAlive = false;

        for (int f = 0; f < numFrames; f++) {
            float partOut[4] = {0.0f, 0.0f, 0.0f, 0.0f};

            // [최적화 2] 8샘플 서브샘플링으로 피치 슬라이드 감산 통합
            if ((f & 0x07) == 0 && v.pitchSlide > 0.0f) {
                v.pitchSlide -= v.pitchSlideStep * 8.0f;
                if (v.pitchSlide < 0.0f) v.pitchSlide = 0.0f;
            }

            for (int pi = 0; pi < numActiveParts; pi++) {
                int p = activeParts[pi];
                if (!v.partActive[p]) continue;

                voiceAlive = true;

                // 5-Stage TVA Envelope 서브샘플링 연산 (8샘플마다 1회 상태 머신 갱신으로 CPU 87.5% 절감)
                if ((f & 0x07) == 0 && v.envStage[p] != 3) {
                    v.envLevel[p] += v.envInc[p] * 8.0f;
                    v.envCounter[p] += 8;
                    if (v.envCounter[p] >= v.envDuration[p]) {
                        v.envCounter[p] = 0;
                        const auto& prt = tp.partials[p];
                        float baseLvl = ((float)prt.tva_level / 100.0f) * v.velNorm;

                        if (v.envStage[p] == 0) { // Attack1 -> Attack2
                            v.envStage[p] = 1;
                            float t2 = max(1.0f, (float)prt.tva_envTime[1]);
                            v.envDuration[p] = (uint32_t)(max(0.004f, t2 * 0.004f) * LA32_SAMPLE_RATE);
                            v.envTarget[p] = ((float)prt.tva_envLevel[1] / 100.0f) * baseLvl;
                            v.envInc[p] = (v.envTarget[p] - v.envLevel[p]) / (float)v.envDuration[p];
                        } else if (v.envStage[p] == 1) { // Attack2 -> Decay
                            v.envStage[p] = 2;
                            float t3 = max(1.0f, (float)prt.tva_envTime[2]);
                            v.envDuration[p] = (uint32_t)(max(0.005f, t3 * 0.008f) * LA32_SAMPLE_RATE);
                            v.envTarget[p] = ((float)prt.tva_envLevel[2] / 100.0f) * baseLvl;
                            v.envInc[p] = (v.envTarget[p] - v.envLevel[p]) / (float)v.envDuration[p];
                        } else if (v.envStage[p] == 2) { // Decay -> Sustain
                            v.envStage[p] = 3;
                            v.envDuration[p] = 0x7FFFFFFF;
                            v.envTarget[p] = ((float)prt.tva_envLevel[3] / 100.0f) * baseLvl;
                            v.envLevel[p] = v.envTarget[p];
                            v.envInc[p] = 0.0f;
                        } else if (v.envStage[p] >= 4) { // Release Complete
                            v.partActive[p] = false;
                            continue;
                        }
                    }
                }

                float sample = 0.0f;
                if (v.usePCM[p]) {
                    uint32_t pos = v.pcmOffset[p] + (uint32_t)v.pcmPos[p];
                    if (pos < MT32_PCM_NUM_SAMPLES) {
                        sample = (float)MT32_PCM_ROM[pos] * 0.000030517578f;
                    }
                    v.pcmPos[p] += v.pcmStep[p];
                    if (v.pcmPos[p] >= v.pcmLength[p]) {
                        if (v.pcmLooped[p]) v.pcmPos[p] = 0.0f;
                        else v.partActive[p] = false;
                    }
                } else {
                    float phase = v.oscPhase[p];
                    float invDt = v.invPhaseInc[p];
                    float raw;

                    if (v.isSaw[p]) {
                        raw = (phase * 2.0f - 1.0f) - fastPolyBLEP(phase, invDt);
                    } else {
                        float pw = v.pulseWidth[p];
                        raw = (phase < pw) ? 1.0f : -1.0f;
                        raw += fastPolyBLEP(phase, invDt);
                        float phase2 = phase + (1.0f - pw);
                        if (phase2 >= 1.0f) phase2 -= 1.0f;
                        raw -= fastPolyBLEP(phase2, invDt);
                    }

                    // TVF 1-Pole Low-Pass 필터 적용 (아날로그 따뜻함 형성 & 초고속 연산)
                    v.filterState[p] += v.filterAlpha[p] * (raw - v.filterState[p]);
                    sample = v.filterState[p];

                    float curPhaseInc = v.phaseInc[p];
                    if (v.pitchSlide > 0.0001f) {
                        curPhaseInc *= (1.0f + v.pitchSlide);
                    }
                    v.oscPhase[p] += curPhaseInc;
                    if (v.oscPhase[p] >= 1.0f) v.oscPhase[p] -= 1.0f;
                }

                partOut[p] = sample * v.envLevel[p];
            }

            // Structure 1~13 믹서 합성
            float pair1 = hasRingMod12 ? (partOut[0] * partOut[1] * 2.0f) : (partOut[0] + partOut[1]);
            float pair2 = hasRingMod34 ? (partOut[2] * partOut[3] * 2.0f) : (partOut[2] + partOut[3]);
            float mixedSample = pair1 + pair2;

            mixAccL[f] += mixedSample * scaleL;
            mixAccR[f] += mixedSample * scaleR;
        }

        if (voiceAlive) activeCount++;
        else v.active = false;
    }

    // Float 버퍼에 직접 가산 (형변환 오버헤드 0화 및 완벽한 무손실 32-bit Float 합성)
    if (activeCount > 0) {
        float* ptr = buffer;
        for (int f = 0; f < numFrames; f++) {
            ptr[0] += mixAccL[f];
            ptr[1] += mixAccR[f];
            ptr += 2;
        }
    }

    activeVoiceCount = activeCount;
}
