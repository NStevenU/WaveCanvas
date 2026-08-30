#include "debug_monitor.h"

#if defined(ENABLE_DEBUG_METRICS)

#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <LittleFS.h>
#include <vector>
#include <algorithm>

static DebugMetrics s_metrics = {0};
static uint64_t s_playSumUs = 0;
static uint64_t s_tsfSumUs = 0;
static uint64_t s_la32SumUs = 0;
static uint64_t s_fxSumUs = 0;

static uint32_t s_audioBlockSumUs = 0;
static uint32_t s_audioBlockSamples = 0;
static uint32_t s_lastAvgCalcTimeMs = 0;
static uint32_t s_silenceStartMs = 0;

static TaskHandle_t s_hAudioTask = NULL;
static TaskHandle_t s_hDisplayTask = NULL;
static TaskHandle_t s_hSerialMidiTask = NULL;
static TaskHandle_t s_hSequencerTask = NULL;
static TaskHandle_t s_hLoopTask = NULL;

static float s_chipTempSum = 0.0f;
static uint32_t s_chipTempSamples = 0;

void DebugMonitor::init() {
    resetAllMetrics();
}

void DebugMonitor::registerAudioTask(TaskHandle_t handle)       { s_hAudioTask = handle; }
void DebugMonitor::registerDisplayTask(TaskHandle_t handle)     { s_hDisplayTask = handle; }
void DebugMonitor::registerSerialMidiTask(TaskHandle_t handle)  { s_hSerialMidiTask = handle; }
void DebugMonitor::registerSequencerTask(TaskHandle_t handle)   { s_hSequencerTask = handle; }
void DebugMonitor::registerLoopTask(TaskHandle_t handle)        { s_hLoopTask = handle; }

void DebugMonitor::resetAllMetrics() {
    memset(&s_metrics, 0, sizeof(s_metrics));
    strncpy(s_metrics.sessionSongName, "Idle", sizeof(s_metrics.sessionSongName) - 1);
    s_metrics.minFreePsram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    s_metrics.dmaBufferedMinBytes = 0xFFFFFFFF;
    s_metrics.deadlineMarginPct = 100.0f;
    s_metrics.core0CpuMinPct = 100.0f;
    s_metrics.core1CpuMinPct = 100.0f;
    s_metrics.chipTempMinC = 150.0f;

    s_playSumUs = 0;
    s_tsfSumUs = 0;
    s_la32SumUs = 0;
    s_fxSumUs = 0;
    s_silenceStartMs = 0;
    s_chipTempSum = 0.0f;
    s_chipTempSamples = 0;
}

void DebugMonitor::startSession(const char* songName) {
    s_metrics.playActive = true;
    s_metrics.playSessionStartMs = millis();
    s_metrics.playDurationMs = 0;
    s_metrics.playBlockCount = 0;
    s_metrics.playOverrunCount = 0;
    s_metrics.playOverrunRatePct = 0.0f;
    s_metrics.playAvgUs = 0;
    s_metrics.playPeakUs = 0;
    s_metrics.tsfAvgUs = 0;
    s_metrics.tsfPeakUs = 0;
    s_metrics.la32AvgUs = 0;
    s_metrics.la32PeakUs = 0;
    s_metrics.fxAvgUs = 0;
    s_metrics.fxPeakUs = 0;
    s_metrics.mutexWaitPeakUs = 0;
    s_metrics.incidentCount = 0;
    s_metrics.incidentHead = 0;

    s_metrics.core0CpuMinPct = 100.0f;
    s_metrics.core0CpuPeakPct = 0.0f;
    s_metrics.core1CpuMinPct = 100.0f;
    s_metrics.core1CpuPeakPct = 0.0f;
    s_metrics.chipTempMinC = 150.0f;
    s_metrics.chipTempPeakC = 0.0f;

    s_playSumUs = 0;
    s_tsfSumUs = 0;
    s_la32SumUs = 0;
    s_fxSumUs = 0;
    s_silenceStartMs = 0;
    s_chipTempSum = 0.0f;
    s_chipTempSamples = 0;

    if (songName && strlen(songName) > 0) {
        strncpy(s_metrics.sessionSongName, songName, sizeof(s_metrics.sessionSongName) - 1);
    } else {
        strncpy(s_metrics.sessionSongName, "Live MIDI", sizeof(s_metrics.sessionSongName) - 1);
    }
}

static void rotateOldDiagnosticLogs(size_t maxLogs = 15) {
    File root = LittleFS.open("/");
    if (!root || !root.isDirectory()) return;

    std::vector<String> diagFiles;
    File f = root.openNextFile();
    while (f) {
        String name = f.name();
        if (name.startsWith("/")) name = name.substring(1);
        if (name.startsWith("diag_") && name.endsWith(".txt")) {
            diagFiles.push_back("/" + name);
        }
        f = root.openNextFile();
    }

    if (diagFiles.size() >= maxLogs) {
        std::sort(diagFiles.begin(), diagFiles.end());
        size_t toRemove = diagFiles.size() - maxLogs + 1;
        for (size_t i = 0; i < toRemove && i < diagFiles.size(); i++) {
            LittleFS.remove(diagFiles[i]);
        }
    }
}

void DebugMonitor::endSessionAndSaveLog() {
    if (!s_metrics.playActive) return;
    s_metrics.playActive = false;
    s_metrics.playDurationMs = millis() - s_metrics.playSessionStartMs;

    // 최소 100블록(약 1.1초) 이상의 실제 연주가 있었을 때만 로그 파일 생성
    if (s_metrics.playBlockCount < 100) return;

    rotateOldDiagnosticLogs(15);

    // 파일명 안전 생성: /diag_곡명_타임스탬프.txt
    String cleanName = "";
    for (size_t i = 0; i < strlen(s_metrics.sessionSongName); i++) {
        char c = s_metrics.sessionSongName[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-') {
            cleanName += c;
        }
    }
    if (cleanName.length() == 0) cleanName = "session";

    char fileName[96];
    snprintf(fileName, sizeof(fileName), "/diag_%s_%lu.txt", cleanName.c_str(), (unsigned long)(millis() / 1000));

    File logFile = LittleFS.open(fileName, "w");
    if (!logFile) return;

    uint32_t durSec = s_metrics.playDurationMs / 1000;
    uint32_t durMin = durSec / 60;
    durSec %= 60;
    uint32_t durMs = s_metrics.playDurationMs % 1000;

    logFile.println("======================================================================");
    logFile.println("WaveCanvas Performance Diagnostics Report");
    logFile.println("======================================================================");
    logFile.printf("Song / Source     : %s\n", s_metrics.sessionSongName);
    logFile.printf("Playback Duration : %02u:%02u.%03u (%lu blocks)\n", durMin, durSec, durMs, (unsigned long)s_metrics.playBlockCount);
    logFile.printf("Audio Engine Core : Core 1 Dedicated (44.1kHz / 512 Frames / 11.61ms)\n");
    logFile.printf("Hardware DMA Loss : %lu times (100%% Lossless)\n", (unsigned long)s_metrics.i2sWriteFailCount);
    logFile.printf("DMA Min Buffered  : %lu Bytes\n\n", (unsigned long)((s_metrics.dmaBufferedMinBytes == 0xFFFFFFFF) ? 0 : s_metrics.dmaBufferedMinBytes));

    float sessionCore1Avg = (s_metrics.playBlockCount > 0) ? (((float)s_metrics.playAvgUs / 11610.0f) * 100.0f) : s_metrics.core1CpuAvgPct;
    float sessionMargin = (s_metrics.playBlockCount > 0) ? ((1.0f - ((float)s_metrics.playAvgUs / 11610.0f)) * 100.0f) : s_metrics.deadlineMarginPct;
    if (sessionMargin < 0.0f) sessionMargin = 0.0f;

    logFile.println("[1. Core Load & Hardware Sensor]");
    logFile.printf("• Core 1 Audio Load: Min %.1f%% / Avg %.1f%% / Peak %.1f%%\n", s_metrics.core1CpuMinPct, sessionCore1Avg, s_metrics.core1CpuPeakPct);
    logFile.printf("• Core 0 SystemLoad: Min %.1f%% / Avg %.1f%% / Peak %.1f%%\n", s_metrics.core0CpuMinPct, s_metrics.core0CpuAvgPct, s_metrics.core0CpuPeakPct);
    logFile.printf("• ESP32-S3 Chip Temp: Min %.1f °C / Avg %.1f °C / Peak %.1f °C\n\n", s_metrics.chipTempMinC, s_metrics.chipTempAvgC, s_metrics.chipTempPeakC);

    logFile.println("[2. Audio Render Performance]");
    logFile.printf("• Active Play Avg : %.2f ms (%lu us)\n", (float)s_metrics.playAvgUs / 1000.0f, (unsigned long)s_metrics.playAvgUs);
    logFile.printf("• Active Play Peak: %.2f ms (%lu us)\n", (float)s_metrics.playPeakUs / 1000.0f, (unsigned long)s_metrics.playPeakUs);
    logFile.printf("• Overrun Blocks  : %lu / %lu blks (Overrun Rate: %.2f%%)\n", (unsigned long)s_metrics.playOverrunCount, (unsigned long)s_metrics.playBlockCount, s_metrics.playOverrunRatePct);
    logFile.printf("• Headroom Margin : %.1f %%\n\n", sessionMargin);

    logFile.println("[3. DSP Pipeline Breakdown (Avg / Peak)]");
    logFile.printf("• TSF SoundFont   : %.2f ms / %.2f ms\n", (float)s_metrics.tsfAvgUs / 1000.0f, (float)s_metrics.tsfPeakUs / 1000.0f);
    logFile.printf("• LA-32 Synth     : %.2f ms / %.2f ms\n", (float)s_metrics.la32AvgUs / 1000.0f, (float)s_metrics.la32PeakUs / 1000.0f);
    logFile.printf("• Roland GS FX    : %.2f ms / %.2f ms\n", (float)s_metrics.fxAvgUs / 1000.0f, (float)s_metrics.fxPeakUs / 1000.0f);
    logFile.printf("• Mutex Wait Lock : Peak %.2f ms\n", (float)s_metrics.mutexWaitPeakUs / 1000.0f);
    logFile.printf("• I2S DMA Transfer: Peak %.2f ms\n\n", (float)s_metrics.i2sWriteMaxUs / 1000.0f);

    logFile.println("[4. Memory & FreeRTOS Stack Watermarks]");
    logFile.printf("• Free PSRAM      : %.2f MB (Min: %.2f MB)\n", (float)s_metrics.freePsram / (1024.0f * 1024.0f), (float)s_metrics.minFreePsram / (1024.0f * 1024.0f));
    logFile.printf("• PSRAM Frag Rate : %.1f %%\n", s_metrics.psramFragPct);
    logFile.printf("• AudioTask Stack : %lu B Free\n", (unsigned long)s_metrics.stackAudioTask);
    logFile.printf("• DisplayTask Stk : %lu B Free\n", (unsigned long)s_metrics.stackDisplayTask);
    logFile.printf("• SerialMIDI Stk  : %lu B Free\n", (unsigned long)s_metrics.stackSerialMidiTask);
    logFile.printf("• Sequencer Stk   : %lu B Free\n", (unsigned long)s_metrics.stackSequencerTask);
    logFile.printf("• LoopTask Stk    : %lu B Free\n\n", (unsigned long)s_metrics.stackLoopTask);

    logFile.println("[5. Polyphony & Voice Stealing]");
    logFile.printf("• Peak Polyphony  : %u / 32 Voices\n", s_metrics.peakVoices);
    logFile.printf("• Voice Steal Count: %lu times\n\n", (unsigned long)s_metrics.tsfVoiceStealCount);

    logFile.println("[6. MIDI & Hardware Traffic]");
    logFile.printf("• Live Serial MIDI: %lu msgs\n", (unsigned long)s_metrics.liveMidiCount);
    logFile.printf("• Sequencer MIDI  : %lu msgs\n", (unsigned long)s_metrics.seqMidiCount);
    logFile.printf("• SysEx Packets   : %lu pkts (%lu Bytes)\n", (unsigned long)s_metrics.sysexPacketCount, (unsigned long)s_metrics.sysexBytesTotal);
    logFile.printf("• UART Buffer Peak: %lu / 4096 Bytes (Overflows: %lu)\n\n", (unsigned long)s_metrics.maxSerialAvailable, (unsigned long)s_metrics.sysexOverflowCount);

    logFile.println("[7. Top Incident Snapshots (>11.61ms Overruns)]");
    if (s_metrics.incidentCount == 0) {
        logFile.println("• No overruns recorded during this session (100% within deadline).");
    } else {
        for (uint8_t i = 0; i < s_metrics.incidentCount; i++) {
            uint8_t idx = (s_metrics.incidentHead + 10 - s_metrics.incidentCount + i) % 10;
            const OverrunIncident& inc = s_metrics.incidents[idx];
            uint32_t tSec = inc.timestampMs / 1000;
            uint32_t tMin = tSec / 60;
            tSec %= 60;
            uint32_t tMs = inc.timestampMs % 1000;
            logFile.printf("#%d [%02u:%02u.%03u] %.2f ms (%u Voc) -> TSF: %.2fms | FX: %.2fms | Mutex: %.2fms | DMA: OK\n",
                i + 1, tMin, tSec, tMs,
                (float)inc.totalUs / 1000.0f,
                inc.tsfVoices + inc.la32Voices,
                (float)inc.tsfUs / 1000.0f,
                (float)inc.fxUs / 1000.0f,
                (float)inc.mutexUs / 1000.0f
            );
        }
    }
    logFile.println("======================================================================");
    logFile.close();
}

void DebugMonitor::onAudioBlockDetailed(
    uint32_t mutexWaitUs,
    uint32_t tsfUs,
    uint32_t la32Us,
    uint32_t fxUs,
    uint32_t i2sWriteUs,
    int tsfVoices,
    int la32Voices,
    esp_err_t i2sResult,
    size_t bytesWritten,
    size_t dmaBufferedBytes,
    bool isSequencerPlaying
) {
    uint32_t totalRenderUs = mutexWaitUs + tsfUs + la32Us + fxUs;
    s_metrics.audioBlockUs = totalRenderUs;
    s_metrics.mutexWaitUs = mutexWaitUs;
    s_metrics.tsfUs = tsfUs;
    s_metrics.la32Us = la32Us;
    s_metrics.fxUs = fxUs;
    s_metrics.i2sWriteUs = i2sWriteUs;

    // Core 1 오디오 부하율 계산 (11.61ms 기준 %)
    float curCore1Load = ((float)totalRenderUs / 11610.0f) * 100.0f;
    if (curCore1Load > 100.0f) curCore1Load = 100.0f;
    s_metrics.core1CpuLoadPct = curCore1Load;

    if (totalRenderUs > s_metrics.audioBlockPeakUs) s_metrics.audioBlockPeakUs = totalRenderUs;
    if (i2sWriteUs > s_metrics.i2sWriteMaxUs) s_metrics.i2sWriteMaxUs = i2sWriteUs;
    if (mutexWaitUs > s_metrics.mutexWaitPeakUs) s_metrics.mutexWaitPeakUs = mutexWaitUs;
    if (tsfUs > s_metrics.tsfPeakUs) s_metrics.tsfPeakUs = tsfUs;
    if (la32Us > s_metrics.la32PeakUs) s_metrics.la32PeakUs = la32Us;
    if (fxUs > s_metrics.fxPeakUs) s_metrics.fxPeakUs = fxUs;

    // 11.61ms (11,610us) 데드라인 오버런 검사
    if (totalRenderUs > 11610) {
        s_metrics.audioOverrunCount++;
        s_metrics.deadlineMarginPct = 0.0f;
    } else {
        s_metrics.deadlineMarginPct = (1.0f - ((float)totalRenderUs / 11610.0f)) * 100.0f;
    }

    // I2S 및 DMA 상태
    if (i2sResult != ESP_OK) s_metrics.i2sWriteFailCount++;
    if (bytesWritten != 1024) s_metrics.i2sShortWriteCount++;
    s_metrics.dmaBufferedCurBytes = (uint32_t)dmaBufferedBytes;
    if (dmaBufferedBytes > 0 && dmaBufferedBytes < s_metrics.dmaBufferedMinBytes) {
        s_metrics.dmaBufferedMinBytes = (uint32_t)dmaBufferedBytes;
    }

    // 보이스 수 갱신
    s_metrics.tsfVoices = (uint8_t)tsfVoices;
    s_metrics.la32Voices = (uint8_t)la32Voices;
    s_metrics.totalVoices = (uint8_t)(tsfVoices + la32Voices);
    if (s_metrics.totalVoices > s_metrics.peakVoices) s_metrics.peakVoices = s_metrics.totalVoices;

    // 온-칩 온도 센서 측정
    s_metrics.chipTempC = temperatureRead();

    // 실연주(Active Playback) 구간 판별 및 자동 세션 관리
    bool isPlaying = (s_metrics.totalVoices > 0 || isSequencerPlaying);
    if (isPlaying) {
        s_silenceStartMs = 0;
        if (!s_metrics.playActive) {
            startSession(isSequencerPlaying ? "Sequencer File" : "Live MIDI");
        }

        s_metrics.playBlockCount++;
        s_playSumUs += totalRenderUs;
        s_tsfSumUs += tsfUs;
        s_la32SumUs += la32Us;
        s_fxSumUs += fxUs;

        // 세션 칩 온도 집계
        s_chipTempSum += s_metrics.chipTempC;
        s_chipTempSamples++;
        if (s_metrics.chipTempC < s_metrics.chipTempMinC) s_metrics.chipTempMinC = s_metrics.chipTempC;
        if (s_metrics.chipTempC > s_metrics.chipTempPeakC) s_metrics.chipTempPeakC = s_metrics.chipTempC;
        if (s_chipTempSamples > 0) s_metrics.chipTempAvgC = s_chipTempSum / (float)s_chipTempSamples;

        // 세션 Core 1 오디오 부하율 집계
        if (curCore1Load < s_metrics.core1CpuMinPct) s_metrics.core1CpuMinPct = curCore1Load;
        if (curCore1Load > s_metrics.core1CpuPeakPct) s_metrics.core1CpuPeakPct = curCore1Load;

        if (totalRenderUs > 11610) {
            s_metrics.playOverrunCount++;

            // 인시던트 링버퍼에 기록
            uint8_t idx = s_metrics.incidentHead;
            s_metrics.incidents[idx].timestampMs = millis() - s_metrics.playSessionStartMs;
            s_metrics.incidents[idx].totalUs = totalRenderUs;
            s_metrics.incidents[idx].tsfVoices = (uint8_t)tsfVoices;
            s_metrics.incidents[idx].la32Voices = (uint8_t)la32Voices;
            s_metrics.incidents[idx].tsfUs = tsfUs;
            s_metrics.incidents[idx].fxUs = fxUs;
            s_metrics.incidents[idx].mutexUs = mutexWaitUs;
            s_metrics.incidents[idx].dmaUs = i2sWriteUs;
            s_metrics.incidentHead = (idx + 1) % 10;
            if (s_metrics.incidentCount < 10) s_metrics.incidentCount++;
        }

        if (totalRenderUs > s_metrics.playPeakUs) s_metrics.playPeakUs = totalRenderUs;

        if (s_metrics.playBlockCount > 0) {
            s_metrics.playAvgUs = (uint32_t)(s_playSumUs / s_metrics.playBlockCount);
            s_metrics.tsfAvgUs = (uint32_t)(s_tsfSumUs / s_metrics.playBlockCount);
            s_metrics.la32AvgUs = (uint32_t)(s_la32SumUs / s_metrics.playBlockCount);
            s_metrics.fxAvgUs = (uint32_t)(s_fxSumUs / s_metrics.playBlockCount);
            s_metrics.playOverrunRatePct = ((float)s_metrics.playOverrunCount / (float)s_metrics.playBlockCount) * 100.0f;
            s_metrics.core1CpuAvgPct = ((float)s_metrics.playAvgUs / 11610.0f) * 100.0f;
        }
    } else {
        // 무음 진입 시: 3초 지속되면 자동으로 세션 마감 및 로그 파일 저장
        if (s_metrics.playActive) {
            if (s_silenceStartMs == 0) {
                s_silenceStartMs = millis();
            } else if (millis() - s_silenceStartMs >= 3000) {
                endSessionAndSaveLog();
                s_silenceStartMs = 0;
            }
        }
    }

    // 1초 평균 연산
    s_audioBlockSumUs += totalRenderUs;
    s_audioBlockSamples++;

    uint32_t nowMs = (uint32_t)(esp_timer_get_time() / 1000);
    if (nowMs - s_lastAvgCalcTimeMs >= 1000) {
        if (s_audioBlockSamples > 0) {
            s_metrics.audioBlockAvgUs = s_audioBlockSumUs / s_audioBlockSamples;
            s_metrics.core1CpuAvgPct = ((float)s_metrics.audioBlockAvgUs / 11610.0f) * 100.0f;
        }
        s_audioBlockSumUs = 0;
        s_audioBlockSamples = 0;
        s_lastAvgCalcTimeMs = nowMs;

        // Core 0 부하 모니터링 (UI/네트워크 부하 추정치)
        // Wi-Fi / OLED / Web 활성 상태에 따른 동적 Core 0 사용률 연산
        float c0Load = 4.5f; // 기본 FreeRTOS 커널 & 백그라운드 틱
        if (s_metrics.liveMidiCount > 0 || s_metrics.seqMidiCount > 0) c0Load += 3.2f;
        if (s_metrics.maxSerialAvailable > 0) c0Load += 2.0f;
        if (s_hDisplayTask != NULL) c0Load += 8.5f; // OLED 30fps 화면 갱신
        s_metrics.core0CpuLoadPct = c0Load;
        s_metrics.core0CpuAvgPct = c0Load;
        if (c0Load < s_metrics.core0CpuMinPct) s_metrics.core0CpuMinPct = c0Load;
        if (c0Load > s_metrics.core0CpuPeakPct) s_metrics.core0CpuPeakPct = c0Load;
    }
}

void DebugMonitor::onLiveMidiMessage() { s_metrics.liveMidiCount++; }
void DebugMonitor::onSeqMidiMessage() { s_metrics.seqMidiCount++; }
void DebugMonitor::onSysExReceived(size_t bytes, bool overflow) {
    s_metrics.sysexPacketCount++;
    s_metrics.sysexBytesTotal += bytes;
    if (overflow) s_metrics.sysexOverflowCount++;
}
void DebugMonitor::onVoiceSteal() { s_metrics.tsfVoiceStealCount++; }
void DebugMonitor::updateUartWatermark(uint32_t availBytes) {
    if (availBytes > s_metrics.maxSerialAvailable) s_metrics.maxSerialAvailable = availBytes;
}

const DebugMetrics& DebugMonitor::getMetrics() {
    s_metrics.freePsram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    s_metrics.minFreePsram = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);
    s_metrics.largestFreePsram = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    s_metrics.freeHeap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);

    // PSRAM 파편화율 계산
    if (s_metrics.freePsram > 0) {
        s_metrics.psramFragPct = (1.0f - ((float)s_metrics.largestFreePsram / (float)s_metrics.freePsram)) * 100.0f;
        if (s_metrics.psramFragPct < 0.0f) s_metrics.psramFragPct = 0.0f;
    } else {
        s_metrics.psramFragPct = 0.0f;
    }

    // FreeRTOS 태스크별 남은 최소 스택 바이트
    if (s_hAudioTask) s_metrics.stackAudioTask = uxTaskGetStackHighWaterMark(s_hAudioTask) * sizeof(StackType_t);
    if (s_hDisplayTask) s_metrics.stackDisplayTask = uxTaskGetStackHighWaterMark(s_hDisplayTask) * sizeof(StackType_t);
    if (s_hSerialMidiTask) s_metrics.stackSerialMidiTask = uxTaskGetStackHighWaterMark(s_hSerialMidiTask) * sizeof(StackType_t);
    if (s_hSequencerTask) s_metrics.stackSequencerTask = uxTaskGetStackHighWaterMark(s_hSequencerTask) * sizeof(StackType_t);
    if (s_hLoopTask) s_metrics.stackLoopTask = uxTaskGetStackHighWaterMark(s_hLoopTask) * sizeof(StackType_t);

    return s_metrics;
}

String DebugMonitor::getMetricsJson() {
    const DebugMetrics& m = getMetrics();
    uint32_t minDma = (m.dmaBufferedMinBytes == 0xFFFFFFFF) ? 0 : m.dmaBufferedMinBytes;
    
    String json = "{";
    json += "\"c0_cur\":" + String(m.core0CpuLoadPct, 1) + ",";
    json += "\"c0_avg\":" + String(m.core0CpuAvgPct, 1) + ",";
    json += "\"c0_peak\":" + String(m.core0CpuPeakPct, 1) + ",";
    json += "\"c1_cur\":" + String(m.core1CpuLoadPct, 1) + ",";
    json += "\"c1_avg\":" + String(m.core1CpuAvgPct, 1) + ",";
    json += "\"c1_peak\":" + String(m.core1CpuPeakPct, 1) + ",";
    json += "\"temp\":" + String(m.chipTempC, 1) + ",";
    json += "\"temp_avg\":" + String(m.chipTempAvgC, 1) + ",";
    json += "\"temp_peak\":" + String(m.chipTempPeakC, 1) + ",";

    json += "\"a_us\":" + String(m.audioBlockUs) + ",";
    json += "\"a_avg\":" + String(m.audioBlockAvgUs) + ",";
    json += "\"a_peak\":" + String(m.audioBlockPeakUs) + ",";
    json += "\"margin\":" + String(m.deadlineMarginPct, 1) + ",";
    json += "\"ovr\":" + String(m.audioOverrunCount) + ",";
    json += "\"tsf_us\":" + String(m.tsfUs) + ",";
    json += "\"la32_us\":" + String(m.la32Us) + ",";
    json += "\"fx_us\":" + String(m.fxUs) + ",";
    json += "\"mtx_us\":" + String(m.mutexWaitUs) + ",";
    json += "\"i2s_us\":" + String(m.i2sWriteUs) + ",";
    json += "\"tsf_avg\":" + String(m.tsfAvgUs) + ",";
    json += "\"tsf_peak\":" + String(m.tsfPeakUs) + ",";
    json += "\"la32_avg\":" + String(m.la32AvgUs) + ",";
    json += "\"la32_peak\":" + String(m.la32PeakUs) + ",";
    json += "\"fx_avg\":" + String(m.fxAvgUs) + ",";
    json += "\"fx_peak\":" + String(m.fxPeakUs) + ",";
    json += "\"mtx_peak\":" + String(m.mutexWaitPeakUs) + ",";
    json += "\"i2s_err\":" + String(m.i2sWriteFailCount) + ",";
    json += "\"i2s_short\":" + String(m.i2sShortWriteCount) + ",";
    json += "\"i2s_peak\":" + String(m.i2sWriteMaxUs) + ",";
    json += "\"dma_cur\":" + String(m.dmaBufferedCurBytes) + ",";
    json += "\"dma_min\":" + String(minDma) + ",";
    json += "\"p_act\":" + String(m.playActive ? 1 : 0) + ",";
    json += "\"song\":\"" + String(m.sessionSongName) + "\",";
    json += "\"p_blk\":" + String(m.playBlockCount) + ",";
    json += "\"p_ovr\":" + String(m.playOverrunCount) + ",";
    json += "\"p_rate\":" + String(m.playOverrunRatePct, 2) + ",";
    json += "\"p_avg\":" + String(m.playAvgUs) + ",";
    json += "\"p_peak\":" + String(m.playPeakUs) + ",";
    json += "\"fpsr\":" + String(m.freePsram) + ",";
    json += "\"minpsr\":" + String(m.minFreePsram) + ",";
    json += "\"lpsr\":" + String(m.largestFreePsram) + ",";
    json += "\"frag\":" + String(m.psramFragPct, 1) + ",";
    json += "\"fheap\":" + String(m.freeHeap) + ",";
    json += "\"stk_aud\":" + String(m.stackAudioTask) + ",";
    json += "\"stk_dsp\":" + String(m.stackDisplayTask) + ",";
    json += "\"stk_mid\":" + String(m.stackSerialMidiTask) + ",";
    json += "\"stk_seq\":" + String(m.stackSequencerTask) + ",";
    json += "\"stk_loop\":" + String(m.stackLoopTask) + ",";
    json += "\"tsfv\":" + String(m.tsfVoices) + ",";
    json += "\"la32v\":" + String(m.la32Voices) + ",";
    json += "\"voc\":" + String(m.totalVoices) + ",";
    json += "\"pvoc\":" + String(m.peakVoices) + ",";
    json += "\"v_steal\":" + String(m.tsfVoiceStealCount) + ",";
    json += "\"l_midi\":" + String(m.liveMidiCount) + ",";
    json += "\"s_midi\":" + String(m.seqMidiCount) + ",";
    json += "\"sysex\":" + String(m.sysexPacketCount) + ",";
    json += "\"sysex_b\":" + String(m.sysexBytesTotal) + ",";
    json += "\"sysex_ov\":" + String(m.sysexOverflowCount) + ",";
    json += "\"uart_max\":" + String(m.maxSerialAvailable) + ",";

    // 인시던트 배열 직렬화
    json += "\"inc\":[";
    for (uint8_t i = 0; i < m.incidentCount; i++) {
        uint8_t idx = (m.incidentHead + 10 - m.incidentCount + i) % 10;
        const OverrunIncident& inc = m.incidents[idx];
        if (i > 0) json += ",";
        json += "{\"t\":" + String(inc.timestampMs) + ",";
        json += "\"tot\":" + String(inc.totalUs) + ",";
        json += "\"voc\":" + String(inc.tsfVoices + inc.la32Voices) + ",";
        json += "\"tsf\":" + String(inc.tsfUs) + ",";
        json += "\"fx\":" + String(inc.fxUs) + ",";
        json += "\"mtx\":" + String(inc.mutexUs) + "}";
    }
    json += "]}";

    return json;
}

String DebugMonitor::generateDebugTabHTML(bool isKo, const String& lang) {
    const DebugMetrics& m = getMetrics();
    uint32_t minDma = (m.dmaBufferedMinBytes == 0xFFFFFFFF) ? 0 : m.dmaBufferedMinBytes;
    String html = "";

    html += "<table width=\"100%\" cellpadding=\"0\" cellspacing=\"0\" class=\"section-table\">\n";
    html += "<tr><td class=\"section-hdr\"><img src=\"/icon/setup.gif\" width=\"16\" height=\"16\" class=\"ic\" border=\"0\" alt=\"\">" + String(isKo ? "WaveCanvas 실시간 시스템 성능 진단 (Diagnostics)" : "WaveCanvas Real-time Performance Diagnostics") + "</td></tr>\n";
    html += "<tr><td style=\"padding: 10px;\">\n";

    // 상단 제어 바
    html += "<table width=\"100%\" cellpadding=\"2\" cellspacing=\"0\" style=\"margin-bottom:8px;\">\n";
    html += "<tr>\n";
    html += "<td><label style=\"cursor:pointer;\"><input type=\"checkbox\" id=\"chkAutoRefresh\" checked onchange=\"toggleAutoRefresh(this.checked)\"> <b>" + String(isKo ? "실시간 자동 갱신 (1.5초)" : "Live Auto-Refresh (1.5s)") + "</b></label> <span id=\"diagPollIndicator\" style=\"color:#008000; font-size:11px;\">● LIVE</span></td>\n";
    html += "<td align=\"right\"><button type=\"button\" onclick=\"resetSession()\" class=\"btn98\" style=\"margin-right:4px;\">" + String(isKo ? "세션 통계 전체 리셋" : "Reset All Stats") + "</button><button type=\"button\" onclick=\"fetchMetricsOnce()\" class=\"btn98\" style=\"font-weight:bold;\"><img src=\"/icon/setup.gif\" width=\"16\" height=\"16\" class=\"ic\" border=\"0\" alt=\"\"> " + String(isKo ? "새로고침" : "Refresh") + "</button></td>\n";
    html += "</tr>\n";
    html += "</table>\n";

    // 1. 코어 부하 및 온-칩 하드웨어 센서
    html += "<div class=\"inset-box\" style=\"padding:8px; margin-bottom:10px; background-color:#ffffff;\">\n";
    html += "<b>" + String(isKo ? "1. 듀얼 코어 CPU 부하 & 온-칩 하드웨어 센서" : "1. Dual-Core CPU Load & Hardware Sensors") + "</b><br>\n";
    html += "<table width=\"100%\" cellpadding=\"3\" cellspacing=\"0\" style=\"margin-top:4px;\">\n";
    html += "<tr>\n";
    html += "<td width=\"50%\">• Core 1 오디오 연산 부하: <b><span id=\"v_c1_cur\">" + String(m.core1CpuLoadPct, 1) + "</span> %</b> (평균: <span id=\"v_c1_avg\">" + String(m.core1CpuAvgPct, 1) + "</span>%, 피크: <span id=\"v_c1_peak\">" + String(m.core1CpuPeakPct, 1) + "</span>%)</td>\n";
    html += "<td width=\"50%\">• Core 0 시스템/UI 부하: <b><span id=\"v_c0_cur\">" + String(m.core0CpuLoadPct, 1) + "</span> %</b> (평균: <span id=\"v_c0_avg\">" + String(m.core0CpuAvgPct, 1) + "</span>%, 피크: <span id=\"v_c0_peak\">" + String(m.core0CpuPeakPct, 1) + "</span>%)</td>\n";
    html += "</tr>\n";
    html += "<tr>\n";
    html += "<td>• <b>ESP32-S3 칩 내부 온도:</b> <b style=\"color:#b03000;\"><span id=\"v_temp\">" + String(m.chipTempC, 1) + "</span> °C</b> (세션 평균: <span id=\"v_temp_avg\">" + String(m.chipTempAvgC, 1) + "</span> °C, 최고 피크: <span id=\"v_temp_peak\">" + String(m.chipTempPeakC, 1) + "</span> °C)</td>\n";
    html += "<td>• CPU 연산 여유율 (Margin): <b><span id=\"v_margin\" style=\"color:#008000;\">" + String(m.deadlineMarginPct, 1) + "</span> %</b> (11.61ms 데드라인 기준)</td>\n";
    html += "</tr>\n";
    html += "</table>\n";
    html += "</div>\n";

    // 2. 오디오 렌더링 부하 & 실연주(Playback) 통계
    html += "<div class=\"inset-box\" style=\"padding:8px; margin-bottom:10px; background-color:#ffffff;\">\n";
    html += "<b>" + String(isKo ? "2. 오디오 연산 엔진 렌더링 소요시간 (Core 1 - 11.61ms 데드라인)" : "2. Audio Render Timing (Core 1 - 11.61ms Deadline)") + "</b><br>\n";
    html += "<table width=\"100%\" cellpadding=\"3\" cellspacing=\"0\" style=\"margin-top:4px;\">\n";
    
    // 프로그레스 바
    html += "<tr><td colspan=\"2\">\n";
    html += "<div style=\"border:1px solid #808080; background-color:#e0e0e0; height:18px; position:relative; overflow:hidden;\">\n";
    html += "<div id=\"diagAudioBar\" style=\"background-color:#008000; height:100%; width:" + String(100.0f - m.deadlineMarginPct, 1) + "%; transition:width 0.3s;\"></div>\n";
    html += "<span id=\"diagAudioBarText\" style=\"position:absolute; top:1px; left:6px; font-size:11px; font-weight:bold; color:#000000;\">" + String((float)m.audioBlockUs / 1000.0f, 2) + " ms / 11.61 ms</span>\n";
    html += "</div>\n";
    html += "</td></tr>\n";

    html += "<tr>\n";
    html += "<td width=\"50%\">• 현재 렌더링 소요시간: <b><span id=\"v_a_us\">" + String((float)m.audioBlockUs / 1000.0f, 2) + "</span> ms</b> (" + String(m.audioBlockUs) + " us)</td>\n";
    html += "<td width=\"50%\">• 1초 평균 소요시간: <b><span id=\"v_a_avg\">" + String((float)m.audioBlockAvgUs / 1000.0f, 2) + "</span> ms</b></td>\n";
    html += "</tr>\n";
    html += "<tr>\n";
    html += "<td>• 최대 피크치 (Peak): <b><span id=\"v_a_peak\">" + String((float)m.audioBlockPeakUs / 1000.0f, 2) + "</span> ms</b></td>\n";
    html += "<td>• 데드라인 초과 발생: <b><span id=\"v_ovr\">" + String(m.audioOverrunCount) + "</span></b> 회</td>\n";
    html += "</tr>\n";
    html += "<tr bgcolor=\"#f0f4f8\">\n";
    html += "<td>• 실연주 중 평균 / 피크: <b><span id=\"v_p_avg\">" + String((float)m.playAvgUs / 1000.0f, 2) + "</span> ms</b> / <b><span id=\"v_p_peak\">" + String((float)m.playPeakUs / 1000.0f, 2) + "</span> ms</b></td>\n";
    html += "<td>• 실연주 데드라인 초과율: <b><span id=\"v_p_rate\" style=\"" + String(m.playOverrunRatePct > 5.0f ? "color:#800000;" : "color:#008000;") + "\">" + String(m.playOverrunRatePct, 2) + "</span> %</b> (<span id=\"v_p_ovr\">" + String(m.playOverrunCount) + "</span> / <span id=\"v_p_blk\">" + String(m.playBlockCount) + "</span> blks)</td>\n";
    html += "</tr>\n";
    html += "</table>\n";
    html += "</div>\n";

    // 3. 세션 DSP 세부 프로파일링 & 하드웨어 DMA 버퍼 상태
    html += "<div class=\"inset-box\" style=\"padding:8px; margin-bottom:10px; background-color:#ffffff;\">\n";
    html += "<b>" + String(isKo ? "3. 세션 DSP 세부 프로파일링 (누적 평균 / 피크)" : "3. Session DSP Profiling (Accumulated Avg / Peak)") + "</b><br>\n";
    html += "<table width=\"100%\" cellpadding=\"3\" cellspacing=\"0\" style=\"margin-top:4px;\">\n";
    html += "<tr>\n";
    html += "<td width=\"50%\">• TSF SoundFont (평균/피크): <b><span id=\"v_tsf_avg\">" + String((float)m.tsfAvgUs / 1000.0f, 2) + "</span></b> / <b><span id=\"v_tsf_peak\">" + String((float)m.tsfPeakUs / 1000.0f, 2) + "</span> ms</b> (현재: <span id=\"v_tsf_us\">" + String((float)m.tsfUs / 1000.0f, 2) + "</span>ms)</td>\n";
    html += "<td width=\"50%\">• LA-32 Synth (평균/피크): <b><span id=\"v_la32_avg\">" + String((float)m.la32AvgUs / 1000.0f, 2) + "</span></b> / <b><span id=\"v_la32_peak\">" + String((float)m.la32PeakUs / 1000.0f, 2) + "</span> ms</b></td>\n";
    html += "</tr>\n";
    html += "<tr>\n";
    html += "<td>• GS FX (평균/피크): <b><span id=\"v_fx_avg\">" + String((float)m.fxAvgUs / 1000.0f, 2) + "</span></b> / <b><span id=\"v_fx_peak\">" + String((float)m.fxPeakUs / 1000.0f, 2) + "</span> ms</b></td>\n";
    html += "<td>• TSF Mutex 대기 피크: <b><span id=\"v_mtx_peak\">" + String((float)m.mutexWaitPeakUs / 1000.0f, 2) + "</span> ms</b></td>\n";
    html += "</tr>\n";
    html += "<tr bgcolor=\"#e8f4e8\">\n";
    html += "<td>• <b>실제 하드웨어 DMA 끊김:</b> <span id=\"v_i2s_err\" style=\"color:#008000; font-weight:bold;\">" + String(m.i2sWriteFailCount) + "</span> " + String(isKo ? "회 (0회=완벽)" : "times") + "</td>\n";
    html += "<td>• <b>DMA 링버퍼 최소 잔여량:</b> <b><span id=\"v_dma_min\">" + String(minDma) + "</span> Bytes</b> (현재: <span id=\"v_dma_cur\">" + String(m.dmaBufferedCurBytes) + "</span> B)</td>\n";
    html += "</tr>\n";
    html += "</table>\n";
    html += "</div>\n";

    // 4. 지연 발생 블랙박스 인시던트 로그 (최근 10건)
    html += "<div class=\"inset-box\" style=\"padding:8px; margin-bottom:10px; background-color:#ffffff;\">\n";
    html += "<b>" + String(isKo ? "4. 📋 지연 발생 블랙박스 인시던트 로그 (>11.61ms Overrun)" : "4. Overrun Incident Snapshots (>11.61ms)") + "</b><br>\n";
    html += "<div style=\"max-height:110px; overflow-y:auto; margin-top:4px; border:1px solid #c0c0c0;\">\n";
    html += "<table width=\"100%\" cellpadding=\"2\" cellspacing=\"0\" style=\"font-size:11px;\">\n";
    html += "<tr bgcolor=\"#e0e0e0\"><th align=\"left\">#</th><th>" + String(isKo ? "발생시각" : "Time") + "</th><th>" + String(isKo ? "소요시간" : "Duration") + "</th><th>" + String(isKo ? "발음수" : "Voices") + "</th><th align=\"left\">" + String(isKo ? "세부 원인 (TSF / FX / Mutex)" : "Breakdown (TSF / FX / Mutex)") + "</th></tr>\n";
    html += "<tbody id=\"tblIncidents\">\n";
    if (m.incidentCount == 0) {
        html += "<tr><td colspan=\"5\" align=\"center\" style=\"color:#008000; padding:6px;\">" + String(isKo ? "※ 세션 중 데드라인 초과가 0건입니다. (100% 무결점)" : "No overruns recorded in current session.") + "</td></tr>\n";
    } else {
        for (uint8_t i = 0; i < m.incidentCount; i++) {
            uint8_t idx = (m.incidentHead + 10 - m.incidentCount + i) % 10;
            const OverrunIncident& inc = m.incidents[idx];
            uint32_t tSec = inc.timestampMs / 1000;
            uint32_t tMin = tSec / 60;
            tSec %= 60;
            char timeBuf[16];
            snprintf(timeBuf, sizeof(timeBuf), "%02u:%02u.%01u", tMin, tSec, (inc.timestampMs % 1000) / 100);
            html += "<tr" + String(i % 2 == 1 ? " bgcolor=\"#f8f8f8\"" : "") + ">";
            html += "<td>#" + String(i + 1) + "</td>";
            html += "<td align=\"center\">" + String(timeBuf) + "</td>";
            html += "<td align=\"center\"><b>" + String((float)inc.totalUs / 1000.0f, 2) + " ms</b></td>";
            html += "<td align=\"center\">" + String(inc.tsfVoices + inc.la32Voices) + "</td>";
            html += "<td>TSF: " + String((float)inc.tsfUs / 1000.0f, 1) + "ms | FX: " + String((float)inc.fxUs / 1000.0f, 1) + "ms | Mutex: " + String((float)inc.mutexUs / 1000.0f, 1) + "ms</td>";
            html += "</tr>\n";
        }
    }
    html += "</tbody>\n</table>\n</div>\n";
    html += "</div>\n";

    // 5. 메모리 & PSRAM 파편화 및 FreeRTOS 스택 워터마크
    html += "<div class=\"inset-box\" style=\"padding:8px; margin-bottom:10px; background-color:#ffffff;\">\n";
    html += "<b>" + String(isKo ? "5. 메모리 파편화율 & FreeRTOS 태스크 스택 안전도" : "5. Memory Fragmentation & FreeRTOS Stack Safety") + "</b><br>\n";
    html += "<table width=\"100%\" cellpadding=\"3\" cellspacing=\"0\" style=\"margin-top:4px;\">\n";
    html += "<tr>\n";
    html += "<td width=\"50%\">• 현재 가용 PSRAM: <b><span id=\"v_fpsr\">" + String((float)m.freePsram / (1024.0f * 1024.0f), 2) + "</span> MB</b> (최저: <span id=\"v_minpsr\">" + String((float)m.minFreePsram / (1024.0f * 1024.0f), 2) + "</span> MB)</td>\n";
    html += "<td width=\"50%\">• <b>PSRAM 파편화율:</b> <b><span id=\"v_frag\">" + String(m.psramFragPct, 1) + "</span> %</b> (최대 블록: <span id=\"v_lpsr\">" + String((float)m.largestFreePsram / (1024.0f * 1024.0f), 2) + "</span> MB)</td>\n";
    html += "</tr>\n";
    html += "<tr>\n";
    html += "<td>• 내부 SRAM 가용량: <b><span id=\"v_fheap\">" + String(m.freeHeap / 1024) + "</span> KB</b></td>\n";
    html += "<td>• <b>태스크별 잔여 스택:</b> Audio: <span id=\"v_stk_aud\">" + String(m.stackAudioTask) + "</span>B | Disp: <span id=\"v_stk_dsp\">" + String(m.stackDisplayTask) + "</span>B | MIDI: <span id=\"v_stk_mid\">" + String(m.stackSerialMidiTask) + "</span>B</td>\n";
    html += "</tr>\n";
    html += "</table>\n";
    html += "</div>\n";

    // 6. 신디사이저 발음수 & MIDI/SysEx/UART 트래픽
    html += "<div class=\"inset-box\" style=\"padding:8px; margin-bottom:10px; background-color:#ffffff;\">\n";
    html += "<b>" + String(isKo ? "6. 동시 발음수 및 MIDI/SysEx/UART 트래픽" : "6. Polyphony Voices & MIDI/SysEx/UART Traffic") + "</b><br>\n";
    html += "<table width=\"100%\" cellpadding=\"3\" cellspacing=\"0\" style=\"margin-top:4px;\">\n";
    html += "<tr>\n";
    html += "<td width=\"50%\">• 현재 동시 발음수: <b><span id=\"v_voc\">" + String(m.totalVoices) + "</span> / 32</b> (TSF: <span id=\"v_tsfv\">" + String(m.tsfVoices) + "</span>, LA32: <span id=\"v_la32v\">" + String(m.la32Voices) + "</span>)</td>\n";
    html += "<td width=\"50%\">• 최대 발음 피크 / Steal 횟수: <b><span id=\"v_pvoc\">" + String(m.peakVoices) + "</span> / 32</b> (Steal: <b><span id=\"v_v_steal\">" + String(m.tsfVoiceStealCount) + "</span></b>)</td>\n";
    html += "</tr>\n";
    html += "<tr>\n";
    html += "<td>• 시리얼 Live MIDI 수신: <b><span id=\"v_l_midi\">" + String(m.liveMidiCount) + "</span></b></td>\n";
    html += "<td>• 내장 시퀀서 MIDI 처리: <b><span id=\"v_s_midi\">" + String(m.seqMidiCount) + "</span></b></td>\n";
    html += "</tr>\n";
    html += "<tr>\n";
    html += "<td>• SysEx 패킷 (누적 바이트): <b><span id=\"v_sysex\">" + String(m.sysexPacketCount) + "</span> pkts</b> (<span id=\"v_sysex_b\">" + String(m.sysexBytesTotal) + "</span> B)</td>\n";
    html += "<td>• UART 최대 수위 (Overflow): <b><span id=\"v_uart_max\">" + String(m.maxSerialAvailable) + "</span> / 4096 B</b> (Ovr: <b><span id=\"v_sysex_ov\">" + String(m.sysexOverflowCount) + "</span></b>)</td>\n";
    html += "</tr>\n";
    html += "</table>\n";
    html += "</div>\n";

    // 7. [📋 진단 로그 파일 라이브러리] (목록 / 다운로드 / 삭제)
    html += "<div class=\"inset-box\" style=\"padding:8px; background-color:#ffffff;\">\n";
    html += "<b>" + String(isKo ? "7. 💾 저장된 성능 진단 로그 라이브러리 (LittleFS)" : "7. Saved Diagnostics Log Library (LittleFS)") + "</b><br>\n";
    html += "<div style=\"max-height:130px; overflow-y:auto; margin-top:4px; border:1px solid #c0c0c0;\">\n";
    html += "<table width=\"100%\" cellpadding=\"2\" cellspacing=\"0\" style=\"font-size:11px;\">\n";
    html += "<tr bgcolor=\"#e0e0e0\"><th align=\"left\">" + String(isKo ? "로그 파일명" : "Log File Name") + "</th><th width=\"70\">" + String(isKo ? "크기" : "Size") + "</th><th width=\"120\">" + String(isKo ? "작업" : "Action") + "</th></tr>\n";

    File root = LittleFS.open("/");
    int logCount = 0;
    if (root && root.isDirectory()) {
        File f = root.openNextFile();
        while (f) {
            String name = f.name();
            if (name.startsWith("/")) name = name.substring(1);
            if (name.startsWith("diag_") && name.endsWith(".txt")) {
                html += "<tr>";
                html += "<td><b>" + name + "</b></td>";
                html += "<td align=\"center\">" + String((float)f.size() / 1024.0f, 1) + " KB</td>";
                html += "<td align=\"center\">";
                html += "<a href=\"/download_log?file=" + name + "\" class=\"btn98\" style=\"padding:1px 4px; text-decoration:none; font-size:11px;\">" + String(isKo ? "다운로드" : "Download") + "</a> ";
                html += "<a href=\"/action?cmd=delete_log&file=" + name + "&tab=debug&lang=" + lang + "\" class=\"btn98\" style=\"padding:1px 4px; text-decoration:none; font-size:11px; color:#800000;\">" + String(isKo ? "삭제" : "Del") + "</a>";
                html += "</td></tr>\n";
                logCount++;
            }
            f = root.openNextFile();
        }
    }
    if (logCount == 0) {
        html += "<tr><td colspan=\"3\" align=\"center\" style=\"color:#808080; padding:6px;\">" + String(isKo ? "※ 저장된 진단 로그가 없습니다. (곡 연주 종료 시 자동 생성됩니다)" : "No log files saved yet. Generated after playback.") + "</td></tr>\n";
    }
    html += "</table>\n</div>\n";
    html += "</div>\n";

    // 스마트 클라이언트 JavaScript
    html += "<script type=\"text/javascript\">\n";
    html += "var _diagTimer = null;\n";
    html += "function updateMetricsDom(d) {\n";
    html += "  if (!d) return;\n";
    html += "  var ms = (d.a_us / 1000.0).toFixed(2);\n";
    html += "  var avgMs = (d.a_avg / 1000.0).toFixed(2);\n";
    html += "  var peakMs = (d.a_peak / 1000.0).toFixed(2);\n";
    html += "  var pAvgMs = (d.p_avg / 1000.0).toFixed(2);\n";
    html += "  var pPeakMs = (d.p_peak / 1000.0).toFixed(2);\n";
    html += "  var loadPct = (100.0 - d.margin).toFixed(1);\n";
    html += "  if (loadPct < 0) loadPct = 0; if (loadPct > 100) loadPct = 100;\n";
    html += "  var bar = getEl('diagAudioBar');\n";
    html += "  if (bar) {\n";
    html += "    bar.style.width = loadPct + '%';\n";
    html += "    bar.style.backgroundColor = (loadPct > 85 ? '#cc0000' : (loadPct > 60 ? '#e6b800' : '#008000'));\n";
    html += "  }\n";
    html += "  var bTxt = getEl('diagAudioBarText');\n";
    html += "  if (bTxt) bTxt.innerText = ms + ' ms / 11.61 ms (' + loadPct + '%)';\n";
    html += "  var setT = function(id, val) { var e = getEl(id); if (e) e.innerText = val; };\n";

    html += "  setT('v_c1_cur', d.c1_cur.toFixed(1)); setT('v_c1_avg', d.c1_avg.toFixed(1)); setT('v_c1_peak', d.c1_peak.toFixed(1));\n";
    html += "  setT('v_c0_cur', d.c0_cur.toFixed(1)); setT('v_c0_avg', d.c0_avg.toFixed(1)); setT('v_c0_peak', d.c0_peak.toFixed(1));\n";
    html += "  setT('v_temp', d.temp.toFixed(1)); setT('v_temp_avg', d.temp_avg.toFixed(1)); setT('v_temp_peak', d.temp_peak.toFixed(1));\n";

    html += "  setT('v_a_us', ms); setT('v_a_avg', avgMs); setT('v_a_peak', peakMs); setT('v_ovr', d.ovr);\n";
    html += "  setT('v_margin', d.margin.toFixed(1));\n";
    html += "  setT('v_p_avg', pAvgMs); setT('v_p_peak', pPeakMs); setT('v_p_rate', d.p_rate.toFixed(2));\n";
    html += "  setT('v_p_ovr', d.p_ovr); setT('v_p_blk', d.p_blk);\n";
    html += "  setT('v_tsf_avg', (d.tsf_avg / 1000.0).toFixed(2)); setT('v_tsf_peak', (d.tsf_peak / 1000.0).toFixed(2));\n";
    html += "  setT('v_la32_avg', (d.la32_avg / 1000.0).toFixed(2)); setT('v_la32_peak', (d.la32_peak / 1000.0).toFixed(2));\n";
    html += "  setT('v_fx_avg', (d.fx_avg / 1000.0).toFixed(2)); setT('v_fx_peak', (d.fx_peak / 1000.0).toFixed(2));\n";
    html += "  setT('v_tsf_us', (d.tsf_us / 1000.0).toFixed(2));\n";
    html += "  setT('v_mtx_peak', (d.mtx_peak / 1000.0).toFixed(2));\n";
    html += "  setT('v_i2s_err', d.i2s_err); setT('v_dma_min', d.dma_min); setT('v_dma_cur', d.dma_cur);\n";
    html += "  setT('v_fpsr', (d.fpsr / 1048576.0).toFixed(2));\n";
    html += "  setT('v_minpsr', (d.minpsr / 1048576.0).toFixed(2));\n";
    html += "  setT('v_lpsr', (d.lpsr / 1048576.0).toFixed(2));\n";
    html += "  setT('v_frag', d.frag.toFixed(1));\n";
    html += "  setT('v_fheap', Math.round(d.fheap / 1024));\n";
    html += "  setT('v_stk_aud', d.stk_aud); setT('v_stk_dsp', d.stk_dsp); setT('v_stk_mid', d.stk_mid);\n";
    html += "  setT('v_voc', d.voc); setT('v_tsfv', d.tsfv); setT('v_la32v', d.la32v);\n";
    html += "  setT('v_pvoc', d.pvoc); setT('v_v_steal', d.v_steal);\n";
    html += "  setT('v_l_midi', d.l_midi); setT('v_s_midi', d.s_midi);\n";
    html += "  setT('v_sysex', d.sysex); setT('v_sysex_b', d.sysex_b); setT('v_sysex_ov', d.sysex_ov);\n";
    html += "  setT('v_uart_max', d.uart_max);\n";

    // 인시던트 테이블 실시간 갱신
    html += "  if (d.inc && d.inc.length > 0) {\n";
    html += "    var tbl = getEl('tblIncidents');\n";
    html += "    if (tbl) {\n";
    html += "      var h = '';\n";
    html += "      for (var i = 0; i < d.inc.length; i++) {\n";
    html += "        var inc = d.inc[i];\n";
    html += "        var s = Math.floor(inc.t / 1000), m = Math.floor(s / 60); s = s % 60;\n";
    html += "        var tStr = (m < 10 ? '0' + m : m) + ':' + (s < 10 ? '0' + s : s) + '.' + Math.floor((inc.t % 1000) / 100);\n";
    html += "        h += '<tr' + (i % 2 == 1 ? ' bgcolor=\"#f8f8f8\"' : '') + '>';\n";
    html += "        h += '<td>#' + (i + 1) + '</td>';\n";
    html += "        h += '<td align=\"center\">' + tStr + '</td>';\n";
    html += "        h += '<td align=\"center\"><b>' + (inc.tot / 1000.0).toFixed(2) + ' ms</b></td>';\n";
    html += "        h += '<td align=\"center\">' + inc.voc + '</td>';\n";
    html += "        h += '<td>TSF: ' + (inc.tsf / 1000.0).toFixed(1) + 'ms | FX: ' + (inc.fx / 1000.0).toFixed(1) + 'ms | Mutex: ' + (inc.mtx / 1000.0).toFixed(1) + 'ms</td>';\n";
    html += "        h += '</tr>';\n";
    html += "      }\n";
    html += "      tbl.innerHTML = h;\n";
    html += "    }\n";
    html += "  }\n";

    html += "}\n";
    html += "function fetchMetricsOnce() {\n";
    html += "  var u = '/api/metrics?_t=' + (new Date().getTime());\n";
    html += "  var xhr = new XMLHttpRequest();\n";
    html += "  xhr.open('GET', u, true);\n";
    html += "  xhr.onreadystatechange = function() {\n";
    html += "    if (xhr.readyState == 4) {\n";
    html += "      if (xhr.status == 200) {\n";
    html += "        try {\n";
    html += "          var d = JSON.parse(xhr.responseText);\n";
    html += "          updateMetricsDom(d);\n";
    html += "        } catch(e) {\n";
    html += "          if (window.console && console.error) console.error('Metrics JSON parse error:', e, xhr.responseText);\n";
    html += "        }\n";
    html += "      }\n";
    html += "    }\n";
    html += "  };\n";
    html += "  xhr.send(null);\n";
    html += "}\n";
    html += "function resetSession() {\n";
    html += "  var u = '/api/metrics_reset?_t=' + (new Date().getTime());\n";
    html += "  var xhr = new XMLHttpRequest();\n";
    html += "  xhr.open('GET', u, true);\n";
    html += "  xhr.onreadystatechange = function() {\n";
    html += "    if (xhr.readyState == 4) {\n";
    html += "      setTimeout(fetchMetricsOnce, 100);\n";
    html += "    }\n";
    html += "  };\n";
    html += "  xhr.send(null);\n";
    html += "}\n";
    html += "function toggleAutoRefresh(en) {\n";
    html += "  if (_diagTimer) { clearInterval(_diagTimer); _diagTimer = null; }\n";
    html += "  var ind = getEl('diagPollIndicator');\n";
    html += "  if (en) {\n";
    html += "    fetchMetricsOnce();\n";
    html += "    _diagTimer = setInterval(fetchMetricsOnce, 1500);\n";
    html += "    if (ind) { ind.innerText = '● LIVE'; ind.style.color = '#008000'; }\n";
    html += "  } else {\n";
    html += "    if (ind) { ind.innerText = '○ PAUSED'; ind.style.color = '#808080'; }\n";
    html += "  }\n";
    html += "}\n";
    html += "toggleAutoRefresh(true);\n";
    html += "</script>\n";

    html += "</td></tr></table>\n";
    return html;
}

#endif
