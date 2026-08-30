#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// ==============================================================================
// WaveCanvas 실시간 성능 계측 & 디버그 모니터 스위치 (Debug Telemetry Switch)
// ==============================================================================
// 아래 정의를 주석 해제하면 실시간 오디오 지연시간, PSRAM 워터마크, 32보이스 부하,
// 대량 SysEx 카운터 및 웹 진단 대시보드(/?tab=debug)가 활성화됩니다.
// 주석 처리(//)하면 모든 계측 코드가 컴파일 타임에 100% 제거되어 제로 오버헤드가 됩니다.
//#define ENABLE_DEBUG_METRICS

#if defined(ENABLE_DEBUG_METRICS)

// 지연 발생 블랙박스 인시던트 스냅샷 (최근 10건 링버퍼)
struct OverrunIncident {
    uint32_t timestampMs;       // 연주 시작 후 경과 시간 (ms)
    uint32_t totalUs;           // 총 연산 소요 시간 (us)
    uint8_t  tsfVoices;         // TSF 보이스 수
    uint8_t  la32Voices;        // LA-32 보이스 수
    uint32_t tsfUs;             // TSF 연산 시간 (us)
    uint32_t fxUs;              // GS FX 연산 시간 (us)
    uint32_t mutexUs;           // Mutex 대기 시간 (us)
    uint32_t dmaUs;             // I2S Write 시간 (us)
};

struct DebugMetrics {
    // 1. 코어 부하 (Core 0 & Core 1 CPU Load %)
    float    core0CpuLoadPct;       // Core 0 실시간 CPU 사용률 (%)
    float    core0CpuAvgPct;        // Core 0 1초 평균 CPU 사용률 (%)
    float    core0CpuPeakPct;       // Core 0 최대 피크 사용률 (%)
    float    core0CpuMinPct;        // Core 0 최저 사용률 (%)
    
    float    core1CpuLoadPct;       // Core 1 실시간 오디오 부하율 (%)
    float    core1CpuAvgPct;        // Core 1 1초 평균 오디오 부하율 (%)
    float    core1CpuPeakPct;       // Core 1 최대 피크 오디오 부하율 (%)
    float    core1CpuMinPct;        // Core 1 최저 오디오 부하율 (%)

    // 2. 하드웨어 온-칩 센서 (ESP32-S3 On-Chip Temperature)
    float    chipTempC;             // 현재 칩 내부 온도 (°C)
    float    chipTempMinC;          // 최저 칩 온도 (°C)
    float    chipTempAvgC;          // 세션 평균 칩 온도 (°C)
    float    chipTempPeakC;         // 최고 피크 칩 온도 (°C)

    // 3. Core 1 오디오 렌더링 엔진 전체 계측 (11,610us 데드라인)
    uint32_t audioBlockUs;          // 최근 1개 블록(512샘플) 전체 소요 시간 (us)
    uint32_t audioBlockAvgUs;       // 최근 1초간 평균 렌더링 소요 시간 (us)
    uint32_t audioBlockPeakUs;      // 부팅 이후 최대 렌더링 소요 시간 피크치 (us)
    float    deadlineMarginPct;     // 11.61ms 기준 CPU 여유 마진율 (0~100%)
    uint32_t audioOverrunCount;     // 11.61ms 데드라인 초과 발생 횟수 (총계)

    // 4. DSP 단계별 실시간 소요시간 (Microseconds us)
    uint32_t mutexWaitUs;           // TSF 뮤텍스 락 획득 대기 시간 (us)
    uint32_t tsfUs;                 // TSF 사운드폰트 합성 소요 시간 (us)
    uint32_t la32Us;                // LA-32 신디사이저 렌더링 소요 시간 (us)
    uint32_t fxUs;                  // 코러스 + 리버브 + EQ + 리미터 연산 시간 (us)
    uint32_t i2sWriteUs;            // i2s_write DMA 블로킹 소요 시간 (us)

    // 5. 실제 하드웨어 오디오 끊김 & DMA 버퍼 상태
    uint32_t i2sWriteFailCount;     // i2s_write 실패(ESP_OK 아님) 횟수
    uint32_t i2sShortWriteCount;    // i2s_write 전송 바이트 미달 횟수
    uint32_t i2sWriteMaxUs;         // i2s_write 최대 대기 시간 피크치 (us)
    uint32_t dmaBufferedMinBytes;   // DMA 링버퍼 최소 잔여량 워터마크 (Bytes)
    uint32_t dmaBufferedCurBytes;   // DMA 링버퍼 현재 잔여 데이터량 (Bytes)

    // 6. 실연주 세션(Active Playback Session) 전용 누적 통계
    bool     playActive;            // 현재 음표 연주 중 여부
    char     sessionSongName[64];   // 현재 세션 곡명
    uint32_t playSessionStartMs;    // 세션 시작 시각 (millis)
    uint32_t playDurationMs;        // 총 실연주 시간 (ms)
    uint32_t playBlockCount;        // 실연주 구간 총 렌더링 블록 수
    uint32_t playOverrunCount;      // 실연주 구간 중 11.61ms 초과 블록 수
    float    playOverrunRatePct;    // 실연주 구간 오버런 발생률 (%)
    uint32_t playAvgUs;             // 실연주 구간 평균 렌더링 소요 시간 (us)
    uint32_t playPeakUs;            // 실연주 구간 최대 피크 렌더링 소요 시간 (us)

    // 7. 세션 DSP 단계별 누적 평균 / 피크 통계
    uint32_t tsfAvgUs;              // 세션 TSF 평균 소요 시간 (us)
    uint32_t tsfPeakUs;             // 세션 TSF 최대 피크 소요 시간 (us)
    uint32_t la32AvgUs;             // 세션 LA-32 평균 소요 시간 (us)
    uint32_t la32PeakUs;            // 세션 LA-32 최대 피크 소요 시간 (us)
    uint32_t fxAvgUs;               // 세션 GS FX 평균 소요 시간 (us)
    uint32_t fxPeakUs;              // 세션 GS FX 최대 피크 소요 시간 (us)
    uint32_t mutexWaitPeakUs;       // 세션 Mutex 대기 최대 피크 시간 (us)

    // 8. 메모리 & PSRAM 안정성 및 파편화 모니터링
    uint32_t freePsram;             // 현재 가용 PSRAM (Bytes)
    uint32_t minFreePsram;          // 부팅 이후 최저 가용 PSRAM 워터마크 (Bytes)
    uint32_t largestFreePsram;      // 최대 연속 가용 PSRAM 블록 크기 (Bytes)
    float    psramFragPct;          // PSRAM 파편화율 (%)
    uint32_t freeHeap;              // 내부 SRAM 가용량 (Bytes)

    // 9. FreeRTOS 태스크별 남은 최소 스택 바이트 (Stack High Watermark)
    uint32_t stackAudioTask;        // AudioTask 남은 스택 (Bytes)
    uint32_t stackDisplayTask;      // DisplayTask 남은 스택 (Bytes)
    uint32_t stackSerialMidiTask;   // SerialMIDITask 남은 스택 (Bytes)
    uint32_t stackSequencerTask;    // SequencerTask 남은 스택 (Bytes)
    uint32_t stackLoopTask;         // LoopTask 남은 스택 (Bytes)

    // 10. 신디사이저 & 보이스 통계
    uint8_t  tsfVoices;             // TSF 사운드폰트 활성 보이스 수 (0~32)
    uint8_t  la32Voices;            // LA-32 신디사이저 활성 보이스 수 (0~8)
    uint8_t  totalVoices;           // 현재 총 활성 보이스 수
    uint8_t  peakVoices;            // 부팅 이후 최대 동시 발음수 피크치
    uint32_t tsfVoiceStealCount;    // 32보이스 한도로 인한 음 회수(Kill) 발생 횟수

    // 11. Core 0 MIDI & SysEx 트래픽 모니터링
    uint32_t liveMidiCount;         // 수신된 시리얼 Live MIDI 메시지 수
    uint32_t seqMidiCount;          // 내장 시퀀서가 처리한 MIDI 메시지 수
    uint32_t sysexPacketCount;      // 수신된 총 SysEx 패킷 수
    uint32_t sysexBytesTotal;       // 수신된 총 SysEx 바이트 수
    uint32_t sysexOverflowCount;    // 512바이트 SysEx 버퍼 초과 발생 횟수
    uint32_t maxSerialAvailable;    // 4096B UART 버퍼 최대 점유량 (Bytes)

    // 12. 블랙박스 인시던트 링버퍼 (최근 10건)
    OverrunIncident incidents[10];
    uint8_t  incidentCount;         // 기록된 인시던트 수 (최대 10)
    uint8_t  incidentHead;          // 링버퍼 인덱스
};

class DebugMonitor {
public:
    static void init();
    static void registerAudioTask(TaskHandle_t handle);
    static void registerDisplayTask(TaskHandle_t handle);
    static void registerSerialMidiTask(TaskHandle_t handle);
    static void registerSequencerTask(TaskHandle_t handle);
    static void registerLoopTask(TaskHandle_t handle);

    static void onAudioBlockDetailed(
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
    );
    static void onLiveMidiMessage();
    static void onSeqMidiMessage();
    static void onSysExReceived(size_t bytes, bool overflow);
    static void onVoiceSteal();
    static void updateUartWatermark(uint32_t availBytes);
    
    static void startSession(const char* songName);
    static void endSessionAndSaveLog();
    static void resetAllMetrics();
    
    static const DebugMetrics& getMetrics();
    static String getMetricsJson();
    static String generateDebugTabHTML(bool isKo, const String& lang);
};

// 제로 오버헤드 훅 매크로 (활성화 시 함수 호출)
#define DEBUG_AUDIO_DETAILED(mWait, tsf, la32, fx, i2sW, vTsf, vLa32, i2sRes, bWritten, dmaBuf, seqPlay) \
    DebugMonitor::onAudioBlockDetailed(mWait, tsf, la32, fx, i2sW, vTsf, vLa32, i2sRes, bWritten, dmaBuf, seqPlay)
#define DEBUG_RECORD_MIDI()              DebugMonitor::onLiveMidiMessage()
#define DEBUG_RECORD_SEQ_MIDI()          DebugMonitor::onSeqMidiMessage()
#define DEBUG_RECORD_SYSEX(sz, ov)       DebugMonitor::onSysExReceived(sz, ov)
#define DEBUG_RECORD_VOICE_STEAL()       DebugMonitor::onVoiceSteal()
#define DEBUG_UPDATE_UART(avail)         DebugMonitor::updateUartWatermark(avail)
#define DEBUG_START_SESSION(name)        DebugMonitor::startSession(name)
#define DEBUG_END_SESSION()              DebugMonitor::endSessionAndSaveLog()

#define DEBUG_REG_AUDIO_TASK(h)          DebugMonitor::registerAudioTask(h)
#define DEBUG_REG_DISPLAY_TASK(h)        DebugMonitor::registerDisplayTask(h)
#define DEBUG_REG_MIDI_TASK(h)           DebugMonitor::registerSerialMidiTask(h)
#define DEBUG_REG_SEQ_TASK(h)            DebugMonitor::registerSequencerTask(h)
#define DEBUG_REG_LOOP_TASK(h)           DebugMonitor::registerLoopTask(h)

#else

// ==============================================================================
// 비활성화 시 완전한 빈 인라인(No-op) -> 컴파일 시 0 바이트 / 0 CPU 사이클
// ==============================================================================
#define DEBUG_AUDIO_DETAILED(mWait, tsf, la32, fx, i2sW, vTsf, vLa32, i2sRes, bWritten, dmaBuf, seqPlay) do {} while(0)
#define DEBUG_RECORD_MIDI()              do {} while(0)
#define DEBUG_RECORD_SEQ_MIDI()          do {} while(0)
#define DEBUG_RECORD_SYSEX(sz, ov)       do {} while(0)
#define DEBUG_RECORD_VOICE_STEAL()       do {} while(0)
#define DEBUG_UPDATE_UART(avail)         do {} while(0)
#define DEBUG_START_SESSION(name)        do {} while(0)
#define DEBUG_END_SESSION()              do {} while(0)

#define DEBUG_REG_AUDIO_TASK(h)          do {} while(0)
#define DEBUG_REG_DISPLAY_TASK(h)        do {} while(0)
#define DEBUG_REG_MIDI_TASK(h)           do {} while(0)
#define DEBUG_REG_SEQ_TASK(h)            do {} while(0)
#define DEBUG_REG_LOOP_TASK(h)           do {} while(0)

#endif
