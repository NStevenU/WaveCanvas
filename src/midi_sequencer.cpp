#include "midi_sequencer.h"
#include "config.h"
#include "midi_parser.h"
#include "audio_engine.h"
#include "la32_synth.h"
#include "led_indicator.h"
#include "display_ui.h"
#include <LittleFS.h>
#include "esp_heap_caps.h"
#include "esp_timer.h"

SequencerState MIDISequencer::state = SEQ_STOPPED;
SynthMode MIDISequencer::songSynthMode = SYNTH_MODE_GM;
char MIDISequencer::songName[64] = {0};

uint8_t* MIDISequencer::midiData = nullptr;
size_t MIDISequencer::midiDataSize = 0;
uint16_t MIDISequencer::timeDivision = 480;
uint32_t MIDISequencer::tempoUsPerQuarter = 500000; // 120 BPM 기본값
unsigned long MIDISequencer::lastTickUs = 0;
MIDISequencer::TrackPointer MIDISequencer::tracks[64];
uint16_t MIDISequencer::numTracks = 0;
uint32_t MIDISequencer::currentTick = 0;
uint32_t MIDISequencer::tickIntervalUs = 1000;

bool MIDISequencer::isMemorySource = false;
bool MIDISequencer::loopEnabled = false;

uint16_t MIDISequencer::channelRPN[16] = {0x7F7F, 0x7F7F, 0x7F7F, 0x7F7F, 0x7F7F, 0x7F7F, 0x7F7F, 0x7F7F, 0x7F7F, 0x7F7F, 0x7F7F, 0x7F7F, 0x7F7F, 0x7F7F, 0x7F7F, 0x7F7F};
uint16_t MIDISequencer::channelNRPN[16] = {0x7F7F, 0x7F7F, 0x7F7F, 0x7F7F, 0x7F7F, 0x7F7F, 0x7F7F, 0x7F7F, 0x7F7F, 0x7F7F, 0x7F7F, 0x7F7F, 0x7F7F, 0x7F7F, 0x7F7F, 0x7F7F};

uint32_t MIDISequencer::readVarLen(const uint8_t** ptr, const uint8_t* end) {
    uint32_t value = 0;
    while (*ptr < end) {
        uint8_t b = *(*ptr)++;
        value = (value << 7) | (b & 0x7F);
        if (!(b & 0x80)) break;
    }
    return value;
}

static void sequencerTask(void* param) {
    while (true) {
        if (MIDISequencer::getState() == SEQ_PLAYING) {
            MIDISequencer::update();
            vTaskDelay(pdMS_TO_TICKS(1)); // 1ms 양보 (micros 누적 보정으로 0.8ms 음표도 템포 밀림 0% 사수 + OLED 화면 부드러움 보장)
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

bool MIDISequencer::begin() {
    state = SEQ_STOPPED;
    TaskHandle_t hSeq = NULL;
    xTaskCreatePinnedToCore(
        sequencerTask,
        "MIDISeq",
        8192, // 8KB 스택으로 증설 (복잡한 16트랙 파싱 스택 안전 확보)
        NULL,
        3, // Core 0에서 UI 태스크보다 높은 우선순위
        &hSeq,
        0  // Core 0 전용
    );
    DEBUG_REG_SEQ_TASK(hSeq);
    return true;
}

void MIDISequencer::setLoop(bool loop) {
    loopEnabled = loop;
}

bool MIDISequencer::isLoopEnabled() {
    return loopEnabled;
}

bool MIDISequencer::parseTracks(const uint8_t* data, size_t size) {
    if (size < 14 || memcmp(data, "MThd", 4) != 0) return false;

    uint16_t headerTracks = ((uint16_t)data[10] << 8) | data[11];
    timeDivision = ((uint16_t)data[12] << 8) | data[13];
    if (timeDivision == 0 || (timeDivision & 0x8000)) timeDivision = 480;

    const uint8_t* ptr = data + 14;
    const uint8_t* endPtr = data + size;

    numTracks = 0;
    for (uint16_t t = 0; t < headerTracks && t < 64 && ptr + 8 <= endPtr; t++) {
        while (ptr + 8 <= endPtr && memcmp(ptr, "MTrk", 4) != 0) {
            ptr++;
        }
        if (ptr + 8 > endPtr) break;

        uint32_t trackLen = ((uint32_t)ptr[4] << 24) | ((uint32_t)ptr[5] << 16) | ((uint32_t)ptr[6] << 8) | ptr[7];
        ptr += 8;

        tracks[t].start = ptr;
        tracks[t].current = ptr;
        tracks[t].end = ptr + trackLen;
        if (tracks[t].end > endPtr) tracks[t].end = endPtr;
        tracks[t].runningStatus = 0;
        tracks[t].isDone = false;

        // 첫 번째 델타 타임 읽기
        const uint8_t* cPtr = tracks[t].current;
        uint32_t delta = 0;
        while (cPtr < tracks[t].end) {
            uint8_t b = *cPtr++;
            delta = (delta << 7) | (b & 0x7F);
            if (!(b & 0x80)) break;
        }
        tracks[t].current = cPtr;
        tracks[t].nextEventTick = delta;

        ptr += trackLen;
        numTracks++;
    }
    return (numTracks > 0);
}

static SynthMode detectInitialSynthMode(const uint8_t* data, size_t size, const char* path) {
    if (!data || size < 14) return SYNTH_MODE_GM;

    // 1단계: 파일 경로명 시그니처 검사 (PM2/PM2/ 또는 MT32, MT-32)
    if (path) {
        String pStr = path;
        pStr.toLowerCase();
        if (pStr.indexOf("/pm2/pm2/") >= 0 || pStr.indexOf("mt-32") >= 0 || pStr.indexOf("mt32") >= 0) {
            return SYNTH_MODE_MT32;
        }
        if (pStr.indexOf("/pm2/pm/") >= 0) {
            return SYNTH_MODE_GM;
        }
    }

    uint16_t tracks = ((uint16_t)data[10] << 8) | data[11];
    const uint8_t* ptr = data + 14;
    const uint8_t* endPtr = data + size;

    uint32_t activeChannelsMask = 0;
    bool hasMT32SysEx = false;
    bool hasGSSysEx = false;
    bool hasGMSysEx = false;
    bool hasCC0 = false;
    bool hasMT32Progs = false;

    for (uint16_t t = 0; t < tracks && t < 64 && ptr + 8 <= endPtr; t++) {
        while (ptr + 8 <= endPtr && memcmp(ptr, "MTrk", 4) != 0) ptr++;
        if (ptr + 8 > endPtr) break;

        uint32_t trackLen = ((uint32_t)ptr[4] << 24) | ((uint32_t)ptr[5] << 16) | ((uint32_t)ptr[6] << 8) | ptr[7];
        ptr += 8;

        const uint8_t* cPtr = ptr;
        const uint8_t* tEnd = ptr + trackLen;
        if (tEnd > endPtr) tEnd = endPtr;

        uint8_t running = 0;
        while (cPtr < tEnd) {
            MIDISequencer::readVarLen(&cPtr, tEnd); // 델타 타임 안전하게 파싱 건너뛰기
            if (cPtr >= tEnd) break;
            uint8_t status = *cPtr;
            if (status & 0x80) {
                cPtr++;
                if (status < 0xF0) running = status;
            } else {
                status = running;
            }

            if (status == 0xFF) {
                if (cPtr >= tEnd) break;
                cPtr++; // meta type
                uint32_t mlen = 0;
                while (cPtr < tEnd) {
                    uint8_t b = *cPtr++;
                    mlen = (mlen << 7) | (b & 0x7F);
                    if (!(b & 0x80)) break;
                }
                cPtr += mlen;
            } else if ((status & 0xF0) == 0xF0 || (status & 0xF0) == 0xF7) {
                uint32_t slen = 0;
                while (cPtr < tEnd) {
                    uint8_t b = *cPtr++;
                    slen = (slen << 7) | (b & 0x7F);
                    if (!(b & 0x80)) break;
                }
                if (slen >= 4 && cPtr + 3 < tEnd && cPtr[0] == 0x41 && cPtr[2] == 0x16) hasMT32SysEx = true;
                else if (slen >= 10 && cPtr + 6 < tEnd && cPtr[0] == 0x41 && cPtr[2] == 0x42) hasGSSysEx = true;
                else if (slen >= 5 && cPtr + 2 < tEnd && cPtr[0] == 0x7E && cPtr[2] == 0x09) hasGMSysEx = true;
                cPtr += slen;
            } else if ((status & 0xF0) == 0x90) {
                uint8_t ch = status & 0x0F;
                if (cPtr + 1 < tEnd) {
                    cPtr++; // note
                    uint8_t vel = *cPtr++;
                    if (vel > 0) activeChannelsMask |= (1 << ch);
                }
            } else if ((status & 0xF0) == 0xC0) {
                uint8_t ch = status & 0x0F;
                if (cPtr < tEnd) {
                    uint8_t prog = *cPtr++;
                    activeChannelsMask |= (1 << ch);
                    if (prog == 68 || prog == 69 || prog == 95 || prog == 96 || prog == 119 || prog == 122) {
                        hasMT32Progs = true;
                    }
                }
            } else if ((status & 0xF0) == 0xB0) {
                uint8_t ch = status & 0x0F;
                if (cPtr + 1 < tEnd) {
                    uint8_t ctrl = *cPtr++;
                    uint8_t val = *cPtr++;
                    if (ctrl == 0 && val > 0) {
                        if (val != 127) {
                            hasCC0 = true;
                        } else if (ch == 9) {
                            // [수정] 드럼 채널(Ch 10)의 CC 0 = 127은 GS 드럼 킷 지정이므로 GS 모드로 판정
                            hasCC0 = true;
                        }
                    }
                }
            } else if ((status & 0xF0) == 0x80 || (status & 0xF0) == 0xA0 || (status & 0xF0) == 0xE0) {
                cPtr += 2;
            } else if ((status & 0xF0) == 0xD0) {
                cPtr += 1;
            }
        }
        ptr += trackLen;
    }

    if (hasMT32SysEx) return SYNTH_MODE_MT32;
    if (hasGSSysEx || hasCC0) return SYNTH_MODE_GS;
    if (hasGMSysEx) return SYNTH_MODE_GM;

    // 2단계: Roland MT-32 하드웨어 파트 구조 (Ch 0 미사용 & Ch 10~15 미사용 & Ch 1~9 사용)
    bool noCh0 = (activeChannelsMask & (1 << 0)) == 0;
    bool noHighCh = (activeChannelsMask & 0xFC00) == 0; // Ch 10~15
    bool hasParts = (activeChannelsMask & 0x03FE) != 0;  // Ch 1~9

    if (noCh0 && noHighCh && hasParts && hasMT32Progs) {
        return SYNTH_MODE_MT32;
    }

    return SYNTH_MODE_GM;
}

bool MIDISequencer::loadFile(const char* path) {
    stop();

    if (midiData && !isMemorySource) {
        free(midiData);
        midiData = nullptr;
    }

    if (!LittleFS.exists(path)) {
        return false;
    }

    File f = LittleFS.open(path, "r");
    if (!f) return false;

    size_t size = f.size();
    if (size == 0) {
        f.close();
        return false;
    }

    // 8MB Octal PSRAM에 MIDI 데이터 버퍼 동적 할당
    uint8_t* pBuf = (uint8_t*)heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!pBuf) {
        pBuf = (uint8_t*)malloc(size); // SRAM 폴백
    }
    if (!pBuf) {
        f.close();
        return false;
    }

    midiData = pBuf;
    isMemorySource = false;
    loopEnabled = false;

    f.read(midiData, size);
    f.close();
    midiDataSize = size;

    if (!parseTracks(midiData, size)) {
        free(midiData);
        midiData = nullptr;
        return false;
    }

    const char* base = strrchr(path, '/');
    strncpy(songName, base ? base + 1 : path, sizeof(songName) - 1);

    songSynthMode = detectInitialSynthMode(midiData, size, path);
    MIDIParser::setSynthMode(songSynthMode);

    currentTick = 0;
    tempoUsPerQuarter = 500000; // 120 BPM
    tickIntervalUs = tempoUsPerQuarter / timeDivision;
    if (tickIntervalUs == 0) tickIntervalUs = 1000;

    return true;
}

bool MIDISequencer::loadMemory(const uint8_t* data, size_t size, const char* name) {
    stop();

    if (midiData && !isMemorySource) {
        free(midiData);
        midiData = nullptr;
    }

    if (!data || size < 14) return false;

    midiData = (uint8_t*)data;
    midiDataSize = size;
    isMemorySource = true;
    loopEnabled = true;

    if (!parseTracks(midiData, size)) {
        midiData = nullptr;
        return false;
    }

    strncpy(songName, name ? name : "Memory", sizeof(songName) - 1);

    songSynthMode = detectInitialSynthMode(midiData, size, name);
    MIDIParser::setSynthMode(songSynthMode);

    currentTick = 0;
    tempoUsPerQuarter = 500000;
    tickIntervalUs = tempoUsPerQuarter / timeDivision;
    if (tickIntervalUs == 0) tickIntervalUs = 1000;

    return true;
}

void MIDISequencer::play() {
    if (midiData && numTracks > 0) {
        if (!isMemorySource) {
            DisplayUI::onExternalMIDIActivity();
        }
        
        // 일시정지(PAUSED) 후 이어듣기(Resume)인 경우: 악기/뱅크/볼륨/채널 상태를 온전히 보존!
        if (state == SEQ_PAUSED) {
            state = SEQ_PLAYING;
            lastTickUs = micros();
            LEDIndicator::setState(LED_PLAYING);
            return;
        }

        // STOP 상태에서 처음부터 재생하는 경우: 16채널 초기화 및 신스 모드 적용
        DEBUG_START_SESSION(songName);
        SynthMode effectiveMode = songSynthMode;
        if (MIDIParser::getSynthPolicy() == SYNTH_POLICY_MANUAL) {
            effectiveMode = (MIDIParser::getManualSubMode() == MANUAL_MODE_GS) ? SYNTH_MODE_GS :
                            (MIDIParser::getManualSubMode() == MANUAL_MODE_MT32) ? SYNTH_MODE_MT32 : SYNTH_MODE_GM;
        }
        MIDIParser::setSynthMode(effectiveMode);
        
        SemaphoreHandle_t mutex = AudioEngine::getMutex();
        if (mutex) xSemaphoreTake(mutex, portMAX_DELAY);
        AudioEngine::systemResetDirect(); // 안전하게 16채널 리셋
        if (effectiveMode == SYNTH_MODE_MT32) {
            AudioEngine::applyMT32ModeDirect();
        } else if (effectiveMode == SYNTH_MODE_GS) {
            AudioEngine::applyGSModeDirect();
        } else {
            AudioEngine::applyGMModeDirect();
        }
        if (mutex) xSemaphoreGive(mutex);

        state = SEQ_PLAYING;
        lastTickUs = micros();
        LEDIndicator::setState(LED_PLAYING);
        DisplayUI::resetChannelDisplay();
    }
}

void MIDISequencer::pause() {
    if (state == SEQ_PLAYING) {
        state = SEQ_PAUSED;
        LEDIndicator::setState(LED_NORMAL);
        AudioEngine::panic();
    }
}

void MIDISequencer::stopInternal() {
    DEBUG_END_SESSION();
    state = SEQ_STOPPED;
    LEDIndicator::setState(LED_NORMAL);
    MIDIParser::setSynthMode(SYNTH_MODE_GM);
    AudioEngine::systemResetDirect(); // 16채널 모든 사운드 및 컨트롤러/리버브 즉시 완전 리셋!
    currentTick = 0;
    lastTickUs = 0;

    // 16채널 GM 초기값 리셋
    for (int i = 0; i < 16; i++) {
        MIDIParser::resetChannelStatus(i);
        channelRPN[i] = 0x7F7F; // RPN Null
        channelNRPN[i] = 0x7F7F; // NRPN Null
    }

    // 트랙 포인터 리셋
    for (uint16_t t = 0; t < numTracks; t++) {
        tracks[t].current = tracks[t].start;
        tracks[t].runningStatus = 0;
        tracks[t].isDone = false;
        tracks[t].nextEventTick = readVarLen(&tracks[t].current, tracks[t].end);
    }
}

void MIDISequencer::stop() {
    if (state == SEQ_STOPPED) return;
    SemaphoreHandle_t mutex = AudioEngine::getMutex();
    if (mutex) xSemaphoreTake(mutex, pdMS_TO_TICKS(50));
    stopInternal();
    if (mutex) xSemaphoreGive(mutex);
}

SequencerState MIDISequencer::getState() { return state; }

const char* MIDISequencer::getCurrentSongName() { return songName; }

void MIDISequencer::processNextEvents() {
    bool allDone = true;

    for (uint16_t t = 0; t < numTracks; t++) {
        if (tracks[t].isDone) continue;
        allDone = false;

        while (tracks[t].current < tracks[t].end && tracks[t].nextEventTick <= currentTick) {
            uint8_t status = *tracks[t].current;

            if (status < 0x80) {
                status = tracks[t].runningStatus;
            } else {
                tracks[t].current++;
                if (status < 0xF0) {
                    tracks[t].runningStatus = status;
                }
            }

            if (status == 0xFF) { // Meta Event
                if (tracks[t].current >= tracks[t].end) { tracks[t].isDone = true; break; }
                uint8_t metaType = *tracks[t].current++;
                uint32_t metaLen = readVarLen(&tracks[t].current, tracks[t].end);
                if (tracks[t].current + metaLen > tracks[t].end) {
                    tracks[t].isDone = true;
                    break;
                }

                if (metaType == 0x2F) { // End of Track
                    tracks[t].isDone = true;
                    break;
                } else if (metaType == 0x51 && metaLen == 3) { // Set Tempo
                    uint32_t us = ((uint32_t)tracks[t].current[0] << 16) |
                                  ((uint32_t)tracks[t].current[1] << 8) |
                                  ((uint32_t)tracks[t].current[2]);
                    if (us > 0) {
                        tempoUsPerQuarter = us;
                        tickIntervalUs = tempoUsPerQuarter / timeDivision;
                        if (tickIntervalUs < 100) tickIntervalUs = 100;
                    }
                }
                tracks[t].current += metaLen;
            } else if (status == 0xF0 || status == 0xF7) { // SysEx Event
                uint32_t sysexLen = readVarLen(&tracks[t].current, tracks[t].end);
                if (tracks[t].current + sysexLen > tracks[t].end) {
                    tracks[t].isDone = true;
                    break;
                }

                const uint8_t* sdata = tracks[t].current;
                int rawLen = sysexLen;
                if (rawLen > 0 && sdata[rawLen - 1] == 0xF7) rawLen--; // EOX 제외 유효 길이

                if (rawLen >= 8 && sdata[0] == 0x41 && sdata[2] == 0x16 && sdata[3] == 0x12 &&
                    sdata[4] == 0x20 && sdata[5] == 0x00 && sdata[6] == 0x00) {
                    // Roland MT-32 LCD Text Display SysEx (OLED 토스트 팝업 연동)
                    char lcdMsg[24] = {0};
                    int textLen = rawLen - 8; // sdata: 41 dev 16 12 20 00 00 [text...] chk
                    if (textLen > 20) textLen = 20;
                    if (textLen > 0) {
                        for (int i = 0; i < textLen; i++) {
                            char c = (char)sdata[7 + i];
                            lcdMsg[i] = (c >= 32 && c <= 126) ? c : ' ';
                        }
                        lcdMsg[textLen] = '\0';
                        DisplayUI::showToast(lcdMsg, 4000);
                    }
                } else if (sysexLen >= 9 && sdata[0] == 0x41 && sdata[2] == 0x16 && sdata[3] == 0x12 &&
                           sdata[4] == 0x10 && sdata[5] == 0x00 && sdata[6] == 0x01) {
                    // Roland MT-32 Reverb Parameter SysEx (F0 41 <dev> 16 12 10 00 01 <mode> <time> <level> <chk> F7)
                    MIDIParser::setSynthMode(SYNTH_MODE_MT32);
                    AudioEngine::setMT32ReverbDirect(sdata[7], sdata[8], (sysexLen >= 11 ? sdata[9] : 64));
                } else if (sysexLen >= 12 && sdata[0] == 0x41 && sdata[2] == 0x16 && sdata[3] == 0x12 &&
                           (sdata[4] == 0x04 || sdata[4] == 0x08)) {
                    // Roland MT-32 Timbre Temp / User Timbre Memory Dump (12 ~ 256바이트 파셜 음색 덤프)
                    MIDIParser::setSynthMode(SYNTH_MODE_MT32);
                    uint8_t targetPart = sdata[5] & 0x07; // Part 1~8 -> Channel 1~8 (Ch 2~9)
                    uint8_t targetCh = targetPart + 1;
                    LA32SynthEngine::setCustomTimbre(targetCh, &sdata[7], sysexLen - 8);
                } else if (sysexLen >= 12 && sysexLen < 60 && sdata[0] == 0x41 && sdata[2] == 0x16 && sdata[3] == 0x12 &&
                           (sdata[4] == 0x03 || sdata[4] == 0x05) && (sdata[6] == 0x00 || sdata[6] == 0x02)) {
                    // Roland MT-32 Patch Temp (Key Shift, Fine Tune, Bender Range)
                    MIDIParser::setSynthMode(SYNTH_MODE_MT32);
                    uint8_t targetPart = sdata[5] & 0x07;
                    uint8_t targetCh = targetPart + 1;
                    if (sysexLen >= 15) {
                        int8_t keyShift = (int8_t)sdata[9] - 24;
                        float fineTune = ((float)sdata[10] - 50.0f) / 50.0f;
                        uint8_t bender = sdata[11];
                        AudioEngine::setChannelKeyShiftDirect(targetCh, keyShift);
                        AudioEngine::setChannelTuningOffsetDirect(targetCh, fineTune);
                        if (bender > 0 && bender <= 24) AudioEngine::setPitchRangeDirect(targetCh, (float)bender);
                    }
                } else if (sysexLen >= 10 && sdata[0] == 0x41 && sdata[2] == 0x16 && sdata[3] == 0x12 &&
                           sdata[4] == 0x03 && sdata[5] == 0x01 && sdata[6] == 0x10) {
                    // Roland MT-32 Rhythm Temp
                    uint8_t key = sdata[7];
                    uint8_t level = (sysexLen >= 12) ? sdata[9] : 100;
                    uint8_t pan = (sysexLen >= 13) ? sdata[10] : 7;
                    AudioEngine::setDrumKeyLevelDirect(key, level);
                    AudioEngine::setDrumKeyPanDirect(key, pan);
                } else if (sysexLen >= 4 && sdata[0] == 0x41 && sdata[2] == 0x16) {
                    // Roland MT-32 SysEx (F0 41 <dev> 16 ...)
                    if (MIDIParser::getSynthPolicy() == SYNTH_POLICY_AUTO) {
                        MIDIParser::setSynthMode(SYNTH_MODE_MT32);
                        AudioEngine::applyMT32ModeDirect();
                    }
                } else if (sysexLen >= 10 && sdata[0] == 0x41 && sdata[2] == 0x42 && sdata[3] == 0x12 &&
                           sdata[4] == 0x40 && sdata[5] == 0x00 && sdata[6] == 0x7F) {
                    // Roland GS Reset
                    if (MIDIParser::getSynthPolicy() == SYNTH_POLICY_AUTO) {
                        MIDIParser::setSynthMode(SYNTH_MODE_GS);
                        AudioEngine::applyGSModeDirect();
                        AudioEngine::systemResetDirect();
                    }
                } else if (sysexLen >= 11 && sdata[0] == 0x41 && sdata[2] == 0x42 && sdata[3] == 0x12 &&
                           sdata[4] == 0x40 && sdata[5] == 0x02 && sdata[6] == 0x00) {
                    // Roland GS Master EQ (F0 41 <dev> 42 12 40 02 00 <LFreq> <LGain> <HFreq> <HGain> <chk> F7)
                    int8_t lowGain = (int8_t)sdata[8] - 64;
                    int8_t highGain = (int8_t)sdata[10] - 64;
                    AudioEngine::setGSMasterEQDirect(sdata[7], lowGain, sdata[9], highGain);
                } else if (sysexLen >= 10 && sdata[0] == 0x41 && sdata[2] == 0x42 && sdata[3] == 0x12 &&
                           sdata[4] == 0x40 && sdata[5] == 0x01 && sdata[6] == 0x00) {
                    // Roland GS SC-55 LCD Text Display SysEx (F0 41 <dev> 42 12 40 01 00 <text...> <chk> F7)
                    char lcdMsg[36] = {0};
                    int rawLen = sysexLen;
                    if (rawLen > 0 && sdata[rawLen - 1] == 0xF7) rawLen--;
                    int textLen = rawLen - 8;
                    if (textLen > 32) textLen = 32;
                    if (textLen > 0) {
                        for (int i = 0; i < textLen; i++) {
                            char c = (char)sdata[7 + i];
                            lcdMsg[i] = (c >= 32 && c <= 126) ? c : ' ';
                        }
                        lcdMsg[textLen] = '\0';
                        DisplayUI::showToast(lcdMsg, 4000);
                    }
                } else if (sysexLen >= 8 && sdata[0] == 0x41 && sdata[2] == 0x42 && sdata[3] == 0x12 &&
                           sdata[4] == 0x40 && sdata[5] == 0x01 && sdata[6] == 0x30) {
                    // Roland GS Reverb Parameters
                    AudioEngine::setReverbMacroDirect(sdata[7]);
                    if (sysexLen >= 13) {
                        AudioEngine::setGSReverbParamsDirect(sdata[7], sdata[9], sdata[10], sdata[11]);
                    }
                } else if (sysexLen >= 8 && sdata[0] == 0x41 && sdata[2] == 0x42 && sdata[3] == 0x12 &&
                           sdata[4] == 0x40 && sdata[5] == 0x01 && sdata[6] == 0x38) {
                    // Roland GS Chorus Parameters
                    AudioEngine::setChorusMacroDirect(sdata[7]);
                    if (sysexLen >= 14) {
                        AudioEngine::setGSChorusParamsDirect(sdata[9], sdata[10], sdata[11], sdata[12], sdata[13]);
                    }
                } else if (sysexLen >= 8 && sdata[0] == 0x41 && sdata[2] == 0x42 && sdata[3] == 0x12 &&
                           sdata[4] == 0x40 && (sdata[5] & 0xF0) == 0x10) {
                    MIDIParser::setSynthMode(SYNTH_MODE_GS);
                    uint8_t part = sdata[5] & 0x0F;
                    uint8_t targetCh = (part == 0) ? 9 : ((part <= 9) ? (part - 1) : part);
                    if (sdata[6] == 0x08) { // Roland GS Part Vibrato Rate
                        AudioEngine::controlChangeDirect(targetCh, 76, sdata[7]);
                    } else if (sdata[6] == 0x09) { // Roland GS Part Vibrato Depth
                        AudioEngine::controlChangeDirect(targetCh, 77, sdata[7]);
                    } else if (sdata[6] == 0x0A) { // Roland GS Part Vibrato Delay
                        AudioEngine::controlChangeDirect(targetCh, 78, sdata[7]);
                    } else if (sdata[6] == 0x15) { // Roland GS Part Mode (Drum Map: 0=Melody, 1=Drum1, 2=Drum2)
                        bool isDrum = (sdata[7] != 0);
                        AudioEngine::setChannelDrumModeDirect(targetCh, isDrum);
                        if (isDrum) {
                            AudioEngine::setBankDirect(targetCh, 128); // TSF Drum Bank
                            AudioEngine::programChangeDirect(targetCh, 0); // Standard Kit
                        }
                    } else if (sdata[6] == 0x16) { // Roland GS Part Key Shift (-24 ~ +24 semitones)
                        AudioEngine::setChannelKeyShiftDirect(targetCh, (int8_t)sdata[7] - 64);
                    } else if (sdata[6] == 0x18) { // Roland GS Part Fine Tuning
                        AudioEngine::setChannelTuningOffsetDirect(targetCh, ((float)sdata[7] - 64.0f) / 64.0f);
                    } else if (sdata[6] == 0x20) { // Roland GS Part TVF Cutoff
                        AudioEngine::controlChangeDirect(targetCh, 74, sdata[7]);
                    } else if (sdata[6] == 0x21) { // Roland GS Part TVF Resonance
                        AudioEngine::controlChangeDirect(targetCh, 71, sdata[7]);
                    } else if (sdata[6] == 0x22) { // Roland GS Part Attack Time
                        AudioEngine::controlChangeDirect(targetCh, 73, sdata[7]);
                    } else if (sdata[6] == 0x23) { // Roland GS Part Decay Time
                        AudioEngine::controlChangeDirect(targetCh, 75, sdata[7]);
                    } else if (sdata[6] == 0x24) { // Roland GS Part Release Time
                        AudioEngine::controlChangeDirect(targetCh, 72, sdata[7]);
                    } else if (sdata[6] == 0x33) { // Roland GS Part Reverb Send Level
                        AudioEngine::controlChangeDirect(targetCh, 91, sdata[7]);
                    } else if (sdata[6] == 0x34) { // Roland GS Part Chorus Send Level
                        AudioEngine::controlChangeDirect(targetCh, 93, sdata[7]);
                    }
                } else if (sysexLen >= 9 && sdata[0] == 0x41 && sdata[2] == 0x42 && sdata[3] == 0x12 &&
                           sdata[4] == 0x41) {
                    // Roland GS Drum Setup (F0 41 <dev> 42 12 41 <param> <key> <val> <chk> F7)
                    uint8_t param = sdata[5] & 0x0F;
                    uint8_t key = sdata[6];
                    uint8_t val = sdata[7];
                    if (param == 0x01) { // Drum Key Pitch Coarse
                        AudioEngine::setDrumKeyPitchDirect(key, (int8_t)val - 64);
                    } else if (param == 0x04) { // Drum Key Level
                        AudioEngine::setDrumKeyLevelDirect(key, val);
                    } else if (param == 0x05) { // Drum Key Panpot
                        AudioEngine::setDrumKeyPanDirect(key, val);
                    }
                } else if (sysexLen >= 18 && (sdata[0] == 0x7E || sdata[0] == 0x7F) && sdata[2] == 0x08 && sdata[3] == 0x08) {
                    // GM2 Scale/Octave Tuning (F0 7E/7F <dev> 08 08 <chMSB> <chLSB> <12 bytes> F7)
                    int8_t scale[12];
                    for (int i = 0; i < 12; i++) {
                        scale[i] = (int8_t)sdata[6 + i] - 64; // -64 ~ +63 cents
                    }
                    uint16_t chMask = ((uint16_t)sdata[4] << 7) | sdata[5];
                    for (int ch = 0; ch < 16; ch++) {
                        if (chMask & (1 << ch)) {
                            AudioEngine::setScaleTuningDirect(ch, scale);
                        }
                    }
                } else if (sysexLen >= 7 && (sdata[0] == 0x7E || sdata[0] == 0x7F) && sdata[2] == 0x04 && sdata[3] == 0x01) {
                    // Master Volume (F0 7E/7F <dev> 04 01 <lsb> <msb> F7)
                    uint16_t rawVol = (uint16_t)sdata[4] | ((uint16_t)sdata[5] << 7);
                    uint8_t vol = (uint8_t)((rawVol * 100) / 16383);
                    AudioEngine::setMasterVolumeDirect(vol);
                }
                tracks[t].current += sysexLen;
            } else if (status >= 0x80 && status < 0xF0) { // MIDI Voice Message
                DEBUG_RECORD_SEQ_MIDI();
                uint8_t type = status & 0xF0;
                uint8_t ch = status & 0x0F;
                if (tracks[t].current >= tracks[t].end) { tracks[t].isDone = true; break; }
                uint8_t d1 = *tracks[t].current++;
                uint8_t d2 = 0;
                if (type != 0xC0 && type != 0xD0) {
                    if (tracks[t].current >= tracks[t].end) { tracks[t].isDone = true; break; }
                    d2 = *tracks[t].current++;
                }

                switch (type) {
                    case 0x90: // Note On
                        if (d2 > 0) {
                            AudioEngine::noteOnDirect(ch, d1, d2);
                            MIDIParser::setLastActiveChannel(ch);
                            DisplayUI::wakeup();
                            uint8_t vu = (uint8_t)(d2 / 8);
                            if (vu < 2) vu = 2;
                            MIDIParser::setChannelVU(ch, vu);
                            MIDIParser::setChannelLastNoteTime(ch, millis());
                            LEDIndicator::pulseMIDI();
                        } else {
                            AudioEngine::noteOffDirect(ch, d1);
                            MIDIParser::setChannelVU(ch, 0); // 즉시 바닥선으로 복귀
                        }
                        break;
                    case 0x80: // Note Off
                        AudioEngine::noteOffDirect(ch, d1);
                        MIDIParser::setChannelVU(ch, 0); // 즉시 바닥선으로 복귀
                        break;
                    case 0xC0: // Program Change
                        MIDIParser::setChannelProgram(ch, d1);
                        MIDIParser::setLastActiveChannel(ch);
                        AudioEngine::programChangeDirect(ch, d1);
                        break;
                    case 0xB0: // Control Change
                        if (d1 == 0) { // Bank Select MSB
                            if (ch != 9) {
                                if (MIDIParser::getSynthPolicy() == SYNTH_POLICY_AUTO) {
                                    if (d2 > 0) MIDIParser::setSynthMode(SYNTH_MODE_GS);
                                } else if (MIDIParser::getSynthPolicy() == SYNTH_POLICY_MANUAL &&
                                           MIDIParser::getManualSubMode() == MANUAL_MODE_GM) {
                                    d2 = 0; // 수동 GM 모드일 때는 Bank 0 고정
                                }
                            }
                        }
                        if (d1 == 7) {
                            MIDIParser::setChannelVolume(ch, d2);
                        }
                        if (d1 == 11) {
                            MIDIParser::setChannelExpression(ch, d2);
                        }
                        // NRPN / RPN 등록 (8비트 MSB/LSB 결합)
                        if (d1 == 99) { // NRPN MSB
                            channelNRPN[ch] = (channelNRPN[ch] & 0x00FF) | ((uint16_t)d2 << 8);
                            channelRPN[ch] = 0x7F7F;
                            if (MIDIParser::getSynthPolicy() == SYNTH_POLICY_AUTO && MIDIParser::getSynthMode() == SYNTH_MODE_GM) {
                                MIDIParser::setSynthMode(SYNTH_MODE_GS);
                            }
                        }
                        if (d1 == 98) { // NRPN LSB
                            channelNRPN[ch] = (channelNRPN[ch] & 0xFF00) | (d2 & 0x7F);
                            channelRPN[ch] = 0x7F7F;
                            if (MIDIParser::getSynthPolicy() == SYNTH_POLICY_AUTO && MIDIParser::getSynthMode() == SYNTH_MODE_GM) {
                                MIDIParser::setSynthMode(SYNTH_MODE_GS);
                            }
                        }
                        if (d1 == 101) { // RPN MSB
                            channelRPN[ch] = (channelRPN[ch] & 0x00FF) | ((uint16_t)d2 << 8);
                            channelNRPN[ch] = 0x7F7F;
                        }
                        if (d1 == 100) { // RPN LSB
                            channelRPN[ch] = (channelRPN[ch] & 0xFF00) | (d2 & 0x7F);
                            channelNRPN[ch] = 0x7F7F;
                        }

                        if (d1 == 6) { // Data Entry MSB
                            if (channelRPN[ch] == 0x0000) { // Pitch Bend Sensitivity (RPN 00 00)
                                uint8_t range = (d2 > 24) ? 24 : d2;
                                AudioEngine::setPitchRangeDirect(ch, (float)range);
                            } else if (channelNRPN[ch] == 0x0108) { // Vibrato Rate
                                AudioEngine::controlChangeDirect(ch, 76, d2);
                            } else if (channelNRPN[ch] == 0x0109) { // Vibrato Depth
                                AudioEngine::controlChangeDirect(ch, 77, d2);
                            } else if (channelNRPN[ch] == 0x010A) { // Vibrato Delay
                                AudioEngine::controlChangeDirect(ch, 78, d2);
                            } else if (channelNRPN[ch] == 0x0120) { // TVF Cutoff Frequency
                                AudioEngine::controlChangeDirect(ch, 74, d2);
                            } else if (channelNRPN[ch] == 0x0121) { // TVF Resonance
                                AudioEngine::controlChangeDirect(ch, 71, d2);
                            } else if (channelNRPN[ch] == 0x0163) { // Envelope Attack Time
                                AudioEngine::controlChangeDirect(ch, 73, d2);
                            } else if (channelNRPN[ch] == 0x0164) { // Envelope Decay Time
                                AudioEngine::controlChangeDirect(ch, 75, d2);
                            } else if (channelNRPN[ch] == 0x0166) { // Envelope Release Time
                                AudioEngine::controlChangeDirect(ch, 72, d2);
                            } else if ((channelNRPN[ch] >> 8) == 0x18) { // Drum Key Pitch Coarse
                                uint8_t dKey = channelNRPN[ch] & 0x7F;
                                AudioEngine::setDrumKeyPitchDirect(dKey, (int8_t)d2 - 64);
                            } else if ((channelNRPN[ch] >> 8) == 0x1A) { // Drum Key TVF Cutoff
                                uint8_t dKey = channelNRPN[ch] & 0x7F;
                                AudioEngine::setDrumKeyCutoffDirect(dKey, (int8_t)d2 - 64);
                            } else if ((channelNRPN[ch] >> 8) == 0x1C) { // Drum Key Level
                                uint8_t dKey = channelNRPN[ch] & 0x7F;
                                AudioEngine::setDrumKeyLevelDirect(dKey, d2);
                            } else if ((channelNRPN[ch] >> 8) == 0x1D) { // Drum Key Panpot
                                uint8_t dKey = channelNRPN[ch] & 0x7F;
                                AudioEngine::setDrumKeyPanDirect(dKey, d2);
                            }
                        }
                        if (d1 == 121) {
                            channelRPN[ch] = 0x7F7F;
                            channelNRPN[ch] = 0x7F7F;
                        }
                        if (d1 == 120 || d1 == 123) MIDIParser::setChannelVU(ch, 0);
                        if (d1 == 126) AudioEngine::setChannelMonoDirect(ch, true);
                        if (d1 == 127) AudioEngine::setChannelMonoDirect(ch, false);
                        AudioEngine::controlChangeDirect(ch, d1, d2);
                        break;
                    case 0xD0: // Channel Pressure (Aftertouch)
                        AudioEngine::controlChangeDirect(ch, 1, (uint8_t)(d1 / 2)); // 부드러운 모듈레이션 연동
                        break;
                    case 0xA0: // Polyphonic Aftertouch
                        AudioEngine::controlChangeDirect(ch, 1, (uint8_t)(d2 / 2));
                        break;
                    case 0xE0: // Pitch Bend
                        {
                            uint16_t bend = (uint16_t)d1 | ((uint16_t)d2 << 7);
                            AudioEngine::pitchBendDirect(ch, bend);
                        }
                        break;
                }
            }

            if (!tracks[t].isDone) {
                uint32_t delta = readVarLen(&tracks[t].current, tracks[t].end);
                tracks[t].nextEventTick += delta;
            }
        }
    }

    if (allDone) {
        if (loopEnabled && numTracks > 0) {
            // 무한 루프: 트랙 포인터 리셋
            for (uint16_t t = 0; t < numTracks; t++) {
                tracks[t].current = tracks[t].start;
                tracks[t].runningStatus = 0;
                tracks[t].isDone = false;
                const uint8_t* cPtr = tracks[t].current;
                uint32_t delta = 0;
                while (cPtr < tracks[t].end) {
                    uint8_t b = *cPtr++;
                    delta = (delta << 7) | (b & 0x7F);
                    if (!(b & 0x80)) break;
                }
                tracks[t].current = cPtr;
                tracks[t].nextEventTick = delta;
            }
            currentTick = 0;
        } else {
            // [실기 표준 정지 방식]
            // 모든 채널에 댐퍼 페달 해제 및 All Notes Off 전송 (박수/루프 노트 자연 감쇠)
            for (uint8_t ch = 0; ch < 16; ch++) {
                AudioEngine::controlChangeDirect(ch, 64, 0);   // Sustain Off
                AudioEngine::controlChangeDirect(ch, 123, 0);  // All Notes Off
            }

            // 시퀀서 즉시 정지 (UI 볼륨/NVS 플래시 건드리지 않음)
            stopInternal();
        }
    }
}

void MIDISequencer::update() {
    if (state != SEQ_PLAYING) return;
    if (tickIntervalUs == 0) tickIntervalUs = 1000;

    unsigned long nowUs = micros();
    if (lastTickUs == 0) lastTickUs = nowUs;
    unsigned long elapsedUs = nowUs - lastTickUs;

    if (elapsedUs >= tickIntervalUs) {
        uint32_t ticksToAdvance = elapsedUs / tickIntervalUs;
        lastTickUs += ticksToAdvance * tickIntervalUs;

        // 원샷 초고속 일괄 락 (Batch Lock) & 틱 유실 없는 분할 처리:
        SemaphoreHandle_t mutex = AudioEngine::getMutex();
        if (mutex && xSemaphoreTake(mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            // Preserve ordering between live MIDI events and sequencer events.
            AudioEngine::processQueuedMidiEventsLocked();
            while (ticksToAdvance > 0 && state == SEQ_PLAYING) {
                uint32_t step = (ticksToAdvance > 30) ? 30 : ticksToAdvance;
                for (uint32_t i = 0; i < step; i++) {
                    currentTick++;
                    processNextEvents();
                    if (state != SEQ_PLAYING) break;
                }
                ticksToAdvance -= step;
            }
            xSemaphoreGive(mutex);
        }
    }
}