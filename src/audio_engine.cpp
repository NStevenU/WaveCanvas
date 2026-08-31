#include "audio_engine.h"
#include "config.h"
#include "display_ui.h"
#include "chorus_fx.h"
#include "reverb_fx.h"
#include "speaker_eq.h"
#include "master_eq.h"
#include "midi_parser.h"
#include "midi_sequencer.h"
#include "mt32_prog_data.h"
#include "la32_synth.h"
#include <LittleFS.h>
#include <Preferences.h>
#include <driver/i2s.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>
#include "esp_heap_caps.h"

// TSF PSRAM 메모리 할당 매핑 (8MB Octal PSRAM)
// ESP-IDF heap_caps_malloc(0)은 NULL을 반환하므로 size == 0일 때 1바이트로 보정
static inline void* tsf_psram_malloc(size_t size) {
    if (size == 0) size = 1;
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

static inline void* tsf_psram_realloc(void* ptr, size_t size) {
    if (size == 0) size = 1;
    return heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

#define TSF_MALLOC(size)       tsf_psram_malloc(size)
#define TSF_REALLOC(ptr, size) tsf_psram_realloc(ptr, size)
#define TSF_FREE(ptr)          free(ptr)

#define TSF_IMPLEMENTATION
#include "tsf.h"

static tsf* g_tsf = nullptr;
static SemaphoreHandle_t g_tsf_mutex = nullptr;
static QueueHandle_t g_midi_event_queue = nullptr;
static volatile uint32_t g_midi_queue_overflows = 0;

// This queue is deliberately large enough for a burst of UART/SysEx-generated
// MIDI while the audio task is rendering a block.
static constexpr UBaseType_t MIDI_EVENT_QUEUE_LENGTH = 1024;
enum class MidiEventType : uint8_t {
    NoteOn,
    NoteOff,
    ProgramChange,
    ControlChange,
    PitchBend
};
struct MidiEvent {
    MidiEventType type;
    uint8_t channel;
    uint8_t data1;
    uint8_t data2;
    uint16_t value;
};

static void recordMidiQueueOverflow() {
    __atomic_add_fetch(&g_midi_queue_overflows, 1, __ATOMIC_RELAXED);
}

static void enqueueMidiEvent(const MidiEvent& event) {
    if (!g_midi_event_queue ||
        xQueueSend(g_midi_event_queue, &event, 0) != pdTRUE) {
        recordMidiQueueOverflow();
    }
}

void AudioEngine::processQueuedMidiEventsLocked() {
    if (!g_midi_event_queue) return;

    MidiEvent event;
    while (xQueueReceive(g_midi_event_queue, &event, 0) == pdTRUE) {
        switch (event.type) {
            case MidiEventType::NoteOn:
                AudioEngine::noteOnDirect(event.channel, event.data1, event.data2);
                break;
            case MidiEventType::NoteOff:
                AudioEngine::noteOffDirect(event.channel, event.data1);
                break;
            case MidiEventType::ProgramChange:
                AudioEngine::programChangeDirect(event.channel, event.data1);
                break;
            case MidiEventType::ControlChange:
                AudioEngine::controlChangeDirect(event.channel, event.data1, event.data2);
                break;
            case MidiEventType::PitchBend:
                AudioEngine::pitchBendDirect(event.channel, event.value);
                break;
        }
    }
}
static uint8_t g_master_volume = 85;
static bool g_is_mono_mode = false;
static bool g_hw_mono_detected = false;
static char g_current_font_name[64] = "None";
static float g_audio_float_buffer[AUDIO_BUFFER_SIZE * 2]; // 32-bit Float Pipeline
static int16_t g_audio_buffer[AUDIO_BUFFER_SIZE * 2]; // 16-bit Stereo DAC Output
static StereoChorus g_chorus;
static StereoReverb g_reverb;
static MasterEQ g_master_eq;
static SpeakerEQ g_speaker_eq;
int8_t g_drum_pitch[128] = {0};
int8_t g_drum_cutoff[128] = {0};
static uint8_t g_drum_level[128] = {0};
static uint8_t g_drum_pan[128] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

// MT-32 -> GM Drum Map Remapping Table
static const uint8_t MT32_DRUM_MAP[128] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
    16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
    32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 46, 45, 46, 47, // 44: MT-32 Half-Open -> SF2 Open Hi-Hat(46)
    48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63,
    64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79,
    80, 81, 39, 82, 30, 85, 33, 84, // 82:Slap->Clap(39), 83:ScratchPush->Shaker(82), 84:ScratchPull->30, 85:Snap->Castanets(85), 86:Click->Metronome(33), 87:BellTree->84
    88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100, 101, 102, 103,
    104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119,
    120, 121, 122, 123, 124, 125, 126, 127
};

// Roland MT-32 High-Shelf (CM-32L 7kHz+ Presence +2.5dB, Q=0.707) Biquad filter state
// Designed for Fs=44.1kHz: b0=1.2127, b1=-0.9134, b2=0.3319, a1=-0.6060, a2=0.2373
static float s_mt32_hs_z1_L = 0.0f, s_mt32_hs_z2_L = 0.0f;
static float s_mt32_hs_z1_R = 0.0f, s_mt32_hs_z2_R = 0.0f;
static volatile int s_silenceFrames = 100;

void AudioEngine::resetMT32FilterDirect() {
    s_mt32_hs_z1_L = 0.0f;
    s_mt32_hs_z2_L = 0.0f;
    s_mt32_hs_z1_R = 0.0f;
    s_mt32_hs_z2_R = 0.0f;
}

volatile bool AudioEngine::fontLoading = false;
volatile bool AudioEngine::volumeNVSDirty = false;
unsigned long AudioEngine::lastVolumeChangeTime = 0;
static char g_pending_font_path[64] = "";

// LittleFS 파일 스트림 콜백 함수 (SRAM 브릿지 버퍼를 거쳐 PSRAM으로 안전하게 스트리밍)
static int tsf_littlefs_read(void* data, void* ptr, unsigned int size) {
    File* f = (File*)data;
    if (!f || !(*f)) return 0;

    // 4KB 이하의 작은 읽기(헤더, 메타데이터 구조체)는 딜레이 없이 즉시 읽고 복사
    static uint8_t sram_buf[4096];
    if (size <= sizeof(sram_buf)) {
        int res = f->read(sram_buf, size);
        if (res > 0) {
            memcpy(ptr, sram_buf, res);
            return res;
        }
        return 0;
    }

    // 대용량 오디오 샘플 데이터(수 MB)만 4KB씩 쪼개어 복사하며 주기적 CPU 양보
    unsigned int bytes_read = 0;
    uint8_t* out_ptr = (uint8_t*)ptr;

    while (bytes_read < size) {
        unsigned int to_read = size - bytes_read;
        if (to_read > sizeof(sram_buf)) to_read = sizeof(sram_buf);

        int res = f->read(sram_buf, to_read);
        if (res <= 0) break; // EOF 또는 에러
        
        memcpy(out_ptr + bytes_read, sram_buf, res);
        bytes_read += res;

        // 64KB 읽을 때마다 주기적으로 워치독 초기화 & CPU 양보
        if ((bytes_read & 0xFFFF) == 0) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }

    return bytes_read;
}

static int tsf_littlefs_skip(void* data, unsigned int count) {
    File* f = (File*)data;
    if (!f || !(*f)) return 0;
    return f->seek(count, SeekCur) ? 1 : 0;
}

const char* AudioEngine::getCurrentFontName() {
    return g_current_font_name;
}

static bool isCurrentFontCT4MGM() {
    return (strstr(g_current_font_name, "CT4MGM") != nullptr || strstr(g_current_font_name, "ct4mgm") != nullptr);
}

bool AudioEngine::isLoadingFont() {
    return fontLoading;
}

uint8_t AudioEngine::getMasterVolume() {
    return g_master_volume;
}

void AudioEngine::setMasterVolumeDirect(uint8_t volume) {
    if (volume > 100) volume = 100;
    if (g_master_volume != volume) {
        g_master_volume = volume;
        volumeNVSDirty = true;
        lastVolumeChangeTime = millis();
    }
    if (g_tsf) {
        float normVol = (float)volume / 100.0f;
        float gain = normVol * normVol * 0.80f; // Gervill / midis2jam2 reference master gain 0.80
        // GM 및 MT-32 마스터 게인 기준 1:1 완전 일치 (0.80f)
        tsf_set_volume(g_tsf, gain);
    }
}

void AudioEngine::setMasterVolume(uint8_t volume) {
    if (!g_tsf_mutex) {
        setMasterVolumeDirect(volume);
        return;
    }
    if (xSemaphoreTake(g_tsf_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        setMasterVolumeDirect(volume);
        xSemaphoreGive(g_tsf_mutex);
    }
}

void AudioEngine::flushVolumeNVS() {
    if (volumeNVSDirty && (millis() - lastVolumeChangeTime >= 5000)) {
        volumeNVSDirty = false;
        Preferences prefs;
        prefs.begin("audio_cfg", false);
        prefs.putUChar("vol", g_master_volume);
        prefs.end();
    }
}

int AudioEngine::getActiveVoiceCount() {
    if (!g_tsf) return 0;
    return tsf_active_voice_count(g_tsf);
}

bool AudioEngine::initI2S() {
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = AUDIO_SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 6,  // 512 프레임 버퍼와 완벽 동기화 (~34.8ms 락-프리 링버퍼)
        .dma_buf_len = 256,  // 6x256 = 1536 샘플 버퍼
        .use_apll = false,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0
    };

    i2s_pin_config_t pin_config = {
        .bck_io_num = PIN_I2S_BCLK,
        .ws_io_num = PIN_I2S_LRC,
        .data_out_num = PIN_I2S_DOUT,
        .data_in_num = I2S_PIN_NO_CHANGE
    };

    esp_err_t err = i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
    if (err != ESP_OK) {
        return false;
    }

    err = i2s_set_pin(I2S_NUM_0, &pin_config);
    if (err != ESP_OK) {
        return false;
    }

    i2s_set_clk(I2S_NUM_0, AUDIO_SAMPLE_RATE, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);
    i2s_zero_dma_buffer(I2S_NUM_0);
    return true;
}

bool AudioEngine::loadSoundFont(const char* path) {
    fontLoading = true;
    String cleanPath = path;
    if (!cleanPath.startsWith("/")) cleanPath = "/" + cleanPath;

    if (!LittleFS.exists(cleanPath)) {
        DisplayUI::showToast(DisplayUI::isKoreanMode() ? "에러: 폰트 파일 없음" : "Err: Font Not Found", 3000);
        fontLoading = false;
        return false;
    }

    File sfFile = LittleFS.open(cleanPath, "r");
    if (!sfFile) {
        DisplayUI::showToast(DisplayUI::isKoreanMode() ? "에러: 폰트 열기 실패" : "Err: Font Open Fail", 3000);
        fontLoading = false;
        return false;
    }

    size_t fileSize = sfFile.size();

    DisplayUI::showToast(DisplayUI::isKoreanMode() ? "폰트 로딩중..." : "Font Loading...", 5000);

    // 기존 TSF 인스턴스를 먼저 해제하여 PSRAM 공간 확보
    if (g_tsf_mutex) xSemaphoreTake(g_tsf_mutex, portMAX_DELAY);

    if (g_tsf) {
        tsf_close(g_tsf);
        g_tsf = nullptr;
    }

    // 파일 스트림 인터페이스 구성
    struct tsf_stream stream;
    stream.data = &sfFile;
    stream.read = tsf_littlefs_read;
    stream.skip = tsf_littlefs_skip;

    // 파싱 작업은 10~15초 소요되므로 락(Mutex)을 풀어서 다른 코어/태스크가 멈추지 않게 함
    if (g_tsf_mutex) xSemaphoreGive(g_tsf_mutex);

    // 스트림에서 직접 파싱 및 PSRAM 적재
    tsf* new_tsf = tsf_load(&stream);
    sfFile.close();

    // 작업 완료 후 g_tsf 업데이트를 위해 다시 락 획득
    if (g_tsf_mutex) xSemaphoreTake(g_tsf_mutex, portMAX_DELAY);

    if (!new_tsf) {
        if (g_tsf_mutex) xSemaphoreGive(g_tsf_mutex);
        fontLoading = false;
        DisplayUI::showToast(DisplayUI::isKoreanMode() ? "에러: 폰트 파싱 실패" : "Err: Font Parse Fail", 3000);
        // 실패 시 무음 방지를 위해 기본 폰트로 안전 자동 복구
        if (cleanPath != DEFAULT_SF2_FILE && LittleFS.exists(DEFAULT_SF2_FILE)) {
            loadSoundFont(DEFAULT_SF2_FILE);
        }
        return false;
    }

    tsf_set_output(new_tsf, TSF_STEREO_INTERLEAVED, AUDIO_SAMPLE_RATE, -6.0f);
    float normVol = (float)g_master_volume / 100.0f;
    float gain = normVol * normVol * 0.80f; // Gervill / midis2jam2 reference master gain 0.80
    // GM 및 MT-32 마스터 게인 기준 1:1 완전 일치 (0.80f)
    tsf_set_volume(new_tsf, gain);
    if (!tsf_set_max_voices(new_tsf, AUDIO_MAX_VOICES)) {
        Serial.println("[AudioEngine] Warning: tsf_set_max_voices failed to allocate requested voices!");
    }

    g_tsf = new_tsf;

    // 파일 이름 추출 (경로 제외)
    const char* baseName = strrchr(cleanPath.c_str(), '/');
    strncpy(g_current_font_name, baseName ? (baseName + 1) : cleanPath.c_str(), sizeof(g_current_font_name) - 1);

    resetMT32FilterDirect();
    s_silenceFrames = 0;

    if (g_tsf_mutex) xSemaphoreGive(g_tsf_mutex);

    fontLoading = false;
    DisplayUI::showToast(DisplayUI::isKoreanMode() ? "폰트 로드 완료!" : "Font Loaded!");

    return true;
}

// 백그라운드 FreeRTOS 태스크로 비동기 사운드폰트 로드 (Watchdog 크래시 방지)
static void soundFontLoaderTask(void* param) {
    char* path = (char*)param;
    AudioEngine::loadSoundFont(path);
    vTaskDelete(NULL);
}

void AudioEngine::loadSoundFontAsync(const char* path) {
    if (fontLoading) {
        return;
    }
    fontLoading = true; // 레이스 컨디션 방지를 위해 진입 즉시 선점
    strncpy(g_pending_font_path, path, sizeof(g_pending_font_path) - 1);
    
    // Core 0에서 16KB 스택으로 로더 태스크 실행
    BaseType_t res = xTaskCreatePinnedToCore(
        soundFontLoaderTask,
        "SFLoader",
        16384,
        g_pending_font_path,
        2, // 적절한 우선순위
        NULL,
        0  // Core 0
    );
    if (res != pdPASS) {
        fontLoading = false; // 태스크 생성 실패 시 플래그 롤백
    }
}

void AudioEngine::audioTask(void* parameter) {
    size_t bytes_written = 0;

    while (true) {
        int64_t t0 = esp_timer_get_time();
        int64_t t_tsf_start = 0, t_tsf_end = 0;
        int64_t t_la32_start = 0, t_la32_end = 0;
        int64_t t_fx_start = 0, t_fx_end = 0;
        int64_t t_mutex_acquired = 0;

        int curTsfVoices = 0;
        int curLa32Voices = 0;

        if (g_tsf && g_tsf_mutex) {
            if (xSemaphoreTake(g_tsf_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
                t_mutex_acquired = esp_timer_get_time();

                // MIDI producers only enqueue; apply events here so the
                // synthesizer is always accessed under its mutex.
                processQueuedMidiEventsLocked();

                t_tsf_start = t_mutex_acquired;
                tsf_render_float(g_tsf, g_audio_float_buffer, AUDIO_BUFFER_SIZE, 0);
                t_tsf_end = esp_timer_get_time();

                t_la32_start = t_tsf_end;
                LA32SynthEngine::render(g_audio_float_buffer, AUDIO_BUFFER_SIZE);
                t_la32_end = esp_timer_get_time();
                
                curTsfVoices = tsf_active_voice_count(g_tsf);
                curLa32Voices = LA32SynthEngine::getActiveVoiceCount();
                int totalVoices = curTsfVoices + curLa32Voices;
                xSemaphoreGive(g_tsf_mutex);

                if (totalVoices > 0) {
                    s_silenceFrames = 0;
                } else if (s_silenceFrames < 100) {
                    s_silenceFrames++;
                }

                t_fx_start = esp_timer_get_time();
                // 무음 지속 시 DSP 연산 전체 Auto-Bypass (대기 시 Core 1 CPU 0% 절감)
                if (s_silenceFrames >= 85) { // 약 1.0초 리버브 테일 감쇠 후 완전 바이패스
                    memset(g_audio_buffer, 0, sizeof(g_audio_buffer));
                } else {
                    // Roland GS 8종 초경량 스테레오 코러스 (무손실 32-bit Float)
                    g_chorus.process(g_audio_float_buffer, AUDIO_BUFFER_SIZE);

                    // 고품질 스테레오 리버브 (무손실 32-bit Float)
                    g_reverb.process(g_audio_float_buffer, AUDIO_BUFFER_SIZE);

                    // Roland GS 2-Band Master Parametric EQ (무손실 32-bit Float)
                    g_master_eq.process(g_audio_float_buffer, AUDIO_BUFFER_SIZE);

                    // 1-Pass 모노 다운믹스 & 외장 스피커 맞춤 8밴드 PEQ (무손실 32-bit Float)
                    if (g_is_mono_mode || g_hw_mono_detected) {
                        g_speaker_eq.processDownmixAndFilter(g_audio_float_buffer, AUDIO_BUFFER_SIZE);
                    }

                    // Master Ultra-Fast Soft-Clip Limiter & Single Int16 Conversion (안전 헤드룸 구간 무연산 패스스루, 피크 0% 클리핑)
                    int16_t* ptr = g_audio_buffer;
                    const float* fptr = g_audio_float_buffer;
                    for (int i = 0; i < AUDIO_BUFFER_SIZE * 2; i++) {
                        float s = fptr[i];
                        if (s > 0.80f) {
                            float diff = (s - 0.80f) * 4.0f;
                            s = 0.80f + 0.18f * (diff / (1.0f + diff));
                        } else if (s < -0.80f) {
                            float diff = (-s - 0.80f) * 4.0f;
                            s = -(0.80f + 0.18f * (diff / (1.0f + diff)));
                        }
                        ptr[i] = (int16_t)(s * 32767.0f);
                    }
                }
                t_fx_end = esp_timer_get_time();

            } else {
                memset(g_audio_buffer, 0, sizeof(g_audio_buffer));
            }
        } else {
            memset(g_audio_buffer, 0, sizeof(g_audio_buffer));
        }

        uint32_t mutexWaitUs = (t_mutex_acquired > t0) ? (uint32_t)(t_mutex_acquired - t0) : 0;
        uint32_t tsfUs = (t_tsf_end >= t_tsf_start) ? (uint32_t)(t_tsf_end - t_tsf_start) : 0;
        uint32_t la32Us = (t_la32_end >= t_la32_start) ? (uint32_t)(t_la32_end - t_la32_start) : 0;
        uint32_t fxUs = (t_fx_end >= t_fx_start) ? (uint32_t)(t_fx_end - t_fx_start) : 0;

        int64_t t_i2s_start = esp_timer_get_time();
        esp_err_t i2sRes = i2s_write(I2S_NUM_0, g_audio_buffer, sizeof(g_audio_buffer), &bytes_written, portMAX_DELAY);
        int64_t t_i2s_end = esp_timer_get_time();
        uint32_t i2sWriteUs = (uint32_t)(t_i2s_end - t_i2s_start);

        size_t dmaBufferedBytes = 0;

        bool isSeqPlaying = (MIDISequencer::getState() == SEQ_PLAYING);
        DEBUG_AUDIO_DETAILED(mutexWaitUs, tsfUs, la32Us, fxUs, i2sWriteUs, curTsfVoices, curLa32Voices, i2sRes, bytes_written, dmaBufferedBytes, isSeqPlaying);
    }
}



bool AudioEngine::begin() {
    // NVRAM(NVS)에서 저장된 마스터 볼륨 및 오디오 모드 복원
    Preferences prefs;
    prefs.begin("audio_cfg", true);
    g_master_volume = prefs.getUChar("vol", 85);
    g_is_mono_mode = prefs.getBool("mono", false);
    prefs.end();

    LA32SynthEngine::init(); // LA32 신디사이저 엔진 초기화

    g_tsf_mutex = xSemaphoreCreateMutex();
    if (!g_tsf_mutex) {
        return false;
    }
    g_midi_event_queue = xQueueCreate(MIDI_EVENT_QUEUE_LENGTH, sizeof(MidiEvent));
    if (!g_midi_event_queue) {
        vSemaphoreDelete(g_tsf_mutex);
        g_tsf_mutex = nullptr;
        return false;
    }

    if (!initI2S()) {
        vQueueDelete(g_midi_event_queue);
        g_midi_event_queue = nullptr;
        vSemaphoreDelete(g_tsf_mutex);
        g_tsf_mutex = nullptr;
        return false;
    }

    // Core 1에 고우선순위 오디오 렌더링 태스크 생성 (FreeRTOS Core 1 고정)
    TaskHandle_t hAud = NULL;
    BaseType_t res = xTaskCreatePinnedToCore(
        audioTask,
        "AudioTask",
        8192,
        NULL,
        configMAX_PRIORITIES - 1, // 최고 수준 우선순위
        &hAud,
        1 // Core 1 (DSP Core)
    );

    if (res != pdPASS) {
        vQueueDelete(g_midi_event_queue);
        g_midi_event_queue = nullptr;
        vSemaphoreDelete(g_tsf_mutex);
        g_tsf_mutex = nullptr;
        return false;
    }
    DEBUG_REG_AUDIO_TASK(hAud);

    return true;
}

// MIDI 메시지 처리
void AudioEngine::noteOn(uint8_t channel, uint8_t key, uint8_t velocity) {
    enqueueMidiEvent({MidiEventType::NoteOn, channel, key, velocity, 0});
}

void AudioEngine::noteOff(uint8_t channel, uint8_t key) {
    enqueueMidiEvent({MidiEventType::NoteOff, channel, key, 0, 0});
}

void AudioEngine::programChange(uint8_t channel, uint8_t program) {
    enqueueMidiEvent({MidiEventType::ProgramChange, channel, program, 0, 0});
}

void AudioEngine::controlChange(uint8_t channel, uint8_t controller, uint8_t value) {
    enqueueMidiEvent({MidiEventType::ControlChange, channel, controller, value, 0});
}

void AudioEngine::pitchBend(uint8_t channel, uint16_t value) {
    enqueueMidiEvent({MidiEventType::PitchBend, channel, 0, 0, value});
}

uint32_t AudioEngine::getMidiQueueOverflowCount() {
    return __atomic_load_n(&g_midi_queue_overflows, __ATOMIC_RELAXED);
}

SemaphoreHandle_t AudioEngine::getMutex() {
    return g_tsf_mutex;
}

void AudioEngine::noteOnDirect(uint8_t channel, uint8_t key, uint8_t velocity) {
    if (MIDIParser::getSynthMode() == SYNTH_MODE_MT32 && LA32SynthEngine::isChannelCustom(channel)) {
        float panNorm = g_tsf ? tsf_channel_get_pan(g_tsf, channel) : 0.5f;
        LA32SynthEngine::noteOn(channel, key, velocity, panNorm);
        return;
    }

    if (!g_tsf) return;
    bool isDrum = (channel == 9) || tsf_channel_get_drum_mode(g_tsf, channel);
    if (isDrum) {
        // [수정] 오직 MT-32 모드이면서 CT4MGM 폰트일 때만 MT-32 드럼 맵핑 적용 (GS/GM 곡에서는 원본 키 100% 보존)
        bool isMT32Mode = (MIDIParser::getSynthMode() == SYNTH_MODE_MT32 && isCurrentFontCT4MGM());
        uint8_t mappedKey = isMT32Mode ? MT32_DRUM_MAP[key & 0x7F] : key;

        // Roland Sound Canvas & MT-32 드럼 Mute / Choke Group
        if (mappedKey == 42 || mappedKey == 44 || mappedKey == 46) {
            tsf_channel_note_off(g_tsf, channel, 46); // Hi-Hat Group
        } else if (mappedKey == 80 || mappedKey == 81) {
            tsf_channel_note_off(g_tsf, channel, 81); // Triangle Group
        } else if (mappedKey == 78 || mappedKey == 79) {
            tsf_channel_note_off(g_tsf, channel, 79); // Cuica Group
        } else if (mappedKey == 73 || mappedKey == 74) {
            tsf_channel_note_off(g_tsf, channel, 74); // Guiro Group
        } else if (mappedKey == 86 || mappedKey == 87) {
            tsf_channel_note_off(g_tsf, channel, 87); // Bell Group
        }

        uint8_t targetKey = mappedKey;
        
        float vel;
        if (isMT32Mode) {
            vel = MT32_VELO_LUT[velocity & 0x7F];
            if (vel > 1.0f) vel = 1.0f;
            if (vel < 0.001f) vel = 0.001f;
        } else {
            vel = (float)velocity / 127.0f;
        }

        if (g_drum_level[mappedKey & 0x7F] > 0) {
            vel *= ((float)g_drum_level[mappedKey & 0x7F] / 100.0f);
            if (vel > 1.0f) vel = 1.0f;
        }
        uint8_t customPan = g_drum_pan[mappedKey & 0x7F];
        if (customPan <= 127) {
            float oldPan = tsf_channel_get_pan(g_tsf, channel);
            tsf_channel_set_pan(g_tsf, channel, (float)customPan / 127.0f);
            tsf_channel_note_on(g_tsf, channel, (uint8_t)targetKey, vel);
            tsf_channel_set_pan(g_tsf, channel, oldPan);
        } else {
            tsf_channel_note_on(g_tsf, channel, (uint8_t)targetKey, vel);
        }
    } else {
        float vel;
        if (MIDIParser::getSynthMode() == SYNTH_MODE_MT32) {
            vel = MT32_VELO_LUT[velocity & 0x7F];
            if (vel > 1.0f) vel = 1.0f;
            if (vel < 0.001f) vel = 0.001f;
        } else {
            vel = (float)velocity / 127.0f;
        }
        tsf_channel_note_on(g_tsf, channel, key, vel);
    }
}

void AudioEngine::noteOffDirect(uint8_t channel, uint8_t key) {
    if (MIDIParser::getSynthMode() == SYNTH_MODE_MT32 && LA32SynthEngine::isChannelCustom(channel)) {
        LA32SynthEngine::noteOff(channel, key);
        return;
    }

    if (!g_tsf) return;
    tsf_channel_note_off(g_tsf, channel, key);
}

void AudioEngine::setReverbMacroDirect(uint8_t macroType) {
    g_reverb.setMacro(macroType);
}

void AudioEngine::setChorusMacroDirect(uint8_t macroType) {
    g_chorus.setMacro(macroType);
}

void AudioEngine::setGSReverbParamsDirect(uint8_t character, uint8_t level, uint8_t time, uint8_t fb) {
    g_reverb.setGSParameters(character, level, time, fb);
}

void AudioEngine::setGSChorusParamsDirect(uint8_t level, uint8_t fb, uint8_t delay, uint8_t rate, uint8_t depth) {
    g_chorus.setGSParameters(level, fb, delay, rate, depth);
}

void AudioEngine::setMT32ReverbDirect(uint8_t mode, uint8_t time, uint8_t level) {
    // Roland MT-32 Boss DSP 리버브 파라미터 실시간 연동 (0:Room, 1:Hall, 2:Plate, 3:TapDelay)
    g_reverb.setMT32Profile(mode, time, level);
}

void AudioEngine::setDrumKeyPitchDirect(uint8_t key, int8_t pitch) {
    if (key < 128) g_drum_pitch[key] = pitch;
}

void AudioEngine::setDrumKeyCutoffDirect(uint8_t key, int8_t cutoff) {
    if (key < 128) g_drum_cutoff[key] = cutoff;
}

void AudioEngine::setDrumKeyLevelDirect(uint8_t key, uint8_t level) {
    if (key < 128) g_drum_level[key] = level;
}

void AudioEngine::setDrumKeyPanDirect(uint8_t key, uint8_t pan) {
    if (key < 128) g_drum_pan[key] = pan;
}

void AudioEngine::resetDrumKeyParamsDirect() {
    memset(g_drum_pitch, 0, sizeof(g_drum_pitch));
    memset(g_drum_cutoff, 0, sizeof(g_drum_cutoff));
    memset(g_drum_level, 0, sizeof(g_drum_level));
    memset(g_drum_pan, 0xFF, sizeof(g_drum_pan));
}

void AudioEngine::programChangeDirect(uint8_t channel, uint8_t program) {
    if (!g_tsf) return;
    LA32SynthEngine::clearCustomChannel(channel); // 일반 프로그램 체인지 수신 시 사운드폰트로 복귀
    bool isDrum = (channel == 9) || tsf_channel_get_drum_mode(g_tsf, channel);
    tsf_channel_set_presetnumber(g_tsf, channel, program, isDrum ? 1 : 0);
}

void AudioEngine::controlChangeDirect(uint8_t channel, uint8_t controller, uint8_t value) {
    if (!g_tsf) return;
    tsf_channel_midi_control(g_tsf, channel, controller, value);
}

void AudioEngine::pitchBendDirect(uint8_t channel, uint16_t value) {
    if (MIDIParser::getSynthMode() == SYNTH_MODE_MT32 && LA32SynthEngine::isChannelCustom(channel)) {
        float pitchRange = 2.0f;
        if (g_tsf) pitchRange = tsf_channel_get_pitchrange(g_tsf, channel);
        float semitones = ((float)value - 8192.0f) / 8192.0f * pitchRange;
        LA32SynthEngine::pitchBend(channel, semitones);
        return;
    }
    if (!g_tsf) return;
    tsf_channel_set_pitchwheel(g_tsf, channel, (int)value);
}

void AudioEngine::setChannelDrumModeDirect(uint8_t channel, bool isDrum) {
    if (!g_tsf) return;
    tsf_channel_set_drum_mode(g_tsf, channel, isDrum ? 1 : 0);
}

void AudioEngine::setBankDirect(uint8_t channel, uint16_t bank) {
    if (!g_tsf) return;
    tsf_channel_set_bank(g_tsf, channel, bank);
}

void AudioEngine::setBank(uint8_t channel, uint16_t bank) {
    if (!g_tsf || !g_tsf_mutex) return;
    if (xSemaphoreTake(g_tsf_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        setBankDirect(channel, bank);
        xSemaphoreGive(g_tsf_mutex);
    }
}

void AudioEngine::applyMT32ModeDirect() {
    if (!g_tsf) return;
    setMasterVolumeDirect(g_master_volume);
    resetMT32FilterDirect();

    for (int ch = 0; ch < 16; ch++) {
        tsf_channel_set_pitchrange(g_tsf, ch, 12.0f); // MT-32 오리지널 하드웨어 피치벤드 (1옥타브)
        if (ch == 9) {
            tsf_channel_set_drum_mode(g_tsf, 9, 1);
            tsf_channel_set_presetnumber(g_tsf, 9, 127, 1); // CM-64/32 Set (기본 드럼 + 33개 CM-32L SFX 완벽 지원)
        } else {
            tsf_channel_set_drum_mode(g_tsf, ch, 0);
            tsf_channel_set_bank(g_tsf, ch, 127); // MT-32 Melodic Bank 127
        }
    }
    g_master_eq.setParameters(0, 3, 1, 2); // 200Hz Low-Shelf +3.0dB (저역 바디감) & 6kHz High-Shelf +2.0dB (CM-32L Presence 고음 어택감 일체화)
    g_reverb.setMT32Profile(1, 5, 64); // 실기 Clean Hall 1 기본값 (미디 CC/SysEx로 덮어쓰기 허용)
}

void AudioEngine::applyGMModeDirect() {
    LA32SynthEngine::reset();
    if (!g_tsf) return;
    setMasterVolumeDirect(g_master_volume); // GM 기본 볼륨 복귀
    resetMT32FilterDirect();
    for (int ch = 0; ch < 16; ch++) {
        MIDIParser::resetChannelStatus(ch); // MIDIParser 단의 볼륨 100, 익스프레션 127, 프로그램 0 복구!
        tsf_channel_midi_control(g_tsf, ch, 121, 0); // CC 121: 볼륨 100, 익스프레션 127, 팬 64, 필터/엔벨로프 오프셋 0으로 완전 리셋!
        tsf_channel_set_pitchrange(g_tsf, ch, 2.0f); // GM 표준 피치벤드 (온음)
        if (ch == 9) {
            tsf_channel_set_drum_mode(g_tsf, 9, 1);
            tsf_channel_set_presetnumber(g_tsf, 9, 0, 1); // Standard GM Drum Kit
        } else {
            tsf_channel_set_drum_mode(g_tsf, ch, 0);
            tsf_channel_set_bank(g_tsf, ch, 0); // GM Melodic Bank 0
            tsf_channel_set_presetnumber(g_tsf, ch, 0, 0); // GM 기본 Acoustic Piano 0으로 클린 초기화!
        }
    }
    g_master_eq.reset();
    g_reverb.setMacro(2); // Room 3 (GM Default)
    g_chorus.reset();
}

void AudioEngine::applyGSModeDirect() {
    LA32SynthEngine::reset();
    if (!g_tsf) return;
    setMasterVolumeDirect(g_master_volume); // GS 기본 볼륨 복귀
    resetMT32FilterDirect();
    for (int ch = 0; ch < 16; ch++) {
        MIDIParser::resetChannelStatus(ch); // MIDIParser 단의 볼륨/익스프레션 복구!
        tsf_channel_midi_control(g_tsf, ch, 121, 0); // CC 121: 컨트롤러 완전 리셋
        tsf_channel_set_pitchrange(g_tsf, ch, 2.0f); // GS 표준 피치벤드 (온음)
        if (ch == 9) {
            tsf_channel_set_drum_mode(g_tsf, 9, 1);
            tsf_channel_set_presetnumber(g_tsf, 9, 0, 1); // Standard GS Drum Kit
        } else {
            tsf_channel_set_drum_mode(g_tsf, ch, 0);
            tsf_channel_set_presetnumber(g_tsf, ch, 0, 0); // GS 기본 Acoustic Piano 0으로 클린 초기화!
        }
    }
    g_master_eq.reset();
    g_reverb.setMacro(2); // Room 3 (GS Default)
    g_chorus.reset();
}

void AudioEngine::setChannelDrumMode(uint8_t channel, bool isDrum) {
    if (!g_tsf || !g_tsf_mutex) return;
    if (xSemaphoreTake(g_tsf_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        setChannelDrumModeDirect(channel, isDrum);
        xSemaphoreGive(g_tsf_mutex);
    }
}

void AudioEngine::panicDirect() {
    if (g_midi_event_queue) xQueueReset(g_midi_event_queue);
    LA32SynthEngine::reset();
    if (!g_tsf) return;
    tsf_channel_sounds_off_all(g_tsf, -1); // 모든 채널/보이스 즉시 강제 킬
    for (int ch = 0; ch < 16; ch++) {
        tsf_channel_set_sustain(g_tsf, ch, 0);
    }
    g_chorus.reset();
    g_reverb.reset();
    MIDIParser::setSynthMode(SYNTH_MODE_GM);
    applyGMModeDirect();
    MIDIParser::clearAllVU();
}

void AudioEngine::panic() {
    if (!g_tsf || !g_tsf_mutex) return;
    if (xSemaphoreTake(g_tsf_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        panicDirect();
        xSemaphoreGive(g_tsf_mutex);
    }
}

void AudioEngine::systemResetDirect() {
    if (g_midi_event_queue) xQueueReset(g_midi_event_queue);
    LA32SynthEngine::reset();
    resetMT32FilterDirect();
    s_silenceFrames = 0;
    if (!g_tsf) return;
    tsf_channel_sounds_off_all(g_tsf, -1); // 모든 보이스 즉시 강제 킬
    for (int ch = 0; ch < 16; ch++) {
        tsf_channel_set_sustain(g_tsf, ch, 0);
        tsf_channel_midi_control(g_tsf, ch, 121, 0); // All Controllers Off (Sustain, Mod, Pan, Vol 등 리셋)
        tsf_channel_set_bank(g_tsf, ch, 0);          // GM 기본 뱅크 복구
        tsf_channel_set_presetnumber(g_tsf, ch, 0, (ch == 9) ? 1 : 0);
    }
    resetDrumKeyParamsDirect();
    g_master_eq.reset();
    g_chorus.reset();
    g_chorus.setMacro(2); // Chorus 3 기본 코러스 복구
    g_reverb.reset();
    g_reverb.setMacro(2); // Room 3 기본 리버브 복구
}

void AudioEngine::setGSMasterEQDirect(uint8_t lowFreq, int8_t lowGain, uint8_t highFreq, int8_t highGain) {
    g_master_eq.setParameters(lowFreq, lowGain, highFreq, highGain);
}

void AudioEngine::setGSMasterEQ(uint8_t lowFreq, int8_t lowGain, uint8_t highFreq, int8_t highGain) {
    if (!g_tsf_mutex) {
        setGSMasterEQDirect(lowFreq, lowGain, highFreq, highGain);
        return;
    }
    if (xSemaphoreTake(g_tsf_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        setGSMasterEQDirect(lowFreq, lowGain, highFreq, highGain);
        xSemaphoreGive(g_tsf_mutex);
    }
}

void AudioEngine::setChannelKeyShiftDirect(uint8_t channel, int8_t semitones) {
    if (!g_tsf) return;
    tsf_channel_set_tuning_offset(g_tsf, channel, (float)semitones);
}

void AudioEngine::setChannelKeyShift(uint8_t channel, int8_t semitones) {
    if (!g_tsf || !g_tsf_mutex) return;
    if (xSemaphoreTake(g_tsf_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        setChannelKeyShiftDirect(channel, semitones);
        xSemaphoreGive(g_tsf_mutex);
    }
}

void AudioEngine::setChannelTuningOffsetDirect(uint8_t channel, float semitones) {
    if (!g_tsf) return;
    tsf_channel_set_tuning_offset(g_tsf, channel, semitones);
}

void AudioEngine::setChannelTuningOffset(uint8_t channel, float semitones) {
    if (!g_tsf || !g_tsf_mutex) return;
    if (xSemaphoreTake(g_tsf_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        setChannelTuningOffsetDirect(channel, semitones);
        xSemaphoreGive(g_tsf_mutex);
    }
}

void AudioEngine::setScaleTuningDirect(uint8_t channel, const int8_t* scale12) {
    if (!g_tsf || !scale12) return;
    tsf_channel_set_scale_tuning(g_tsf, channel, (const signed char*)scale12);
}

void AudioEngine::setScaleTuning(uint8_t channel, const int8_t* scale12) {
    if (!g_tsf || !g_tsf_mutex || !scale12) return;
    if (xSemaphoreTake(g_tsf_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        setScaleTuningDirect(channel, scale12);
        xSemaphoreGive(g_tsf_mutex);
    }
}

void AudioEngine::setChannelMonoDirect(uint8_t channel, bool isMono) {
    if (!g_tsf) return;
    tsf_channel_set_mono(g_tsf, channel, isMono ? 1 : 0);
}

void AudioEngine::setChannelMono(uint8_t channel, bool isMono) {
    if (!g_tsf || !g_tsf_mutex) return;
    if (xSemaphoreTake(g_tsf_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        setChannelMonoDirect(channel, isMono);
        xSemaphoreGive(g_tsf_mutex);
    }
}

void AudioEngine::setPitchRangeDirect(uint8_t channel, float semitones) {
    if (!g_tsf) return;
    tsf_channel_set_pitchrange(g_tsf, channel, semitones);
}

void AudioEngine::setPitchRange(uint8_t channel, float semitones) {
    if (!g_tsf || !g_tsf_mutex) return;
    if (xSemaphoreTake(g_tsf_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        setPitchRangeDirect(channel, semitones);
        xSemaphoreGive(g_tsf_mutex);
    }
}

void AudioEngine::systemReset() {
    if (!g_tsf || !g_tsf_mutex) return;
    if (xSemaphoreTake(g_tsf_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        systemResetDirect();
        xSemaphoreGive(g_tsf_mutex);
    }
}

void AudioEngine::setMonoMode(bool isMono) {
    g_is_mono_mode = isMono;
    g_speaker_eq.reset();
    Preferences prefs;
    prefs.begin("audio_cfg", false);
    prefs.putBool("mono", isMono);
    prefs.end();
}

bool AudioEngine::isMonoMode() {
    return g_is_mono_mode;
}

void AudioEngine::setHardwareMonoDetected(bool detected) {
    g_hw_mono_detected = detected;
    g_speaker_eq.reset();
}

bool AudioEngine::isHardwareMonoDetected() {
    return g_hw_mono_detected;
}

bool AudioEngine::isEffectiveMono() {
    return g_is_mono_mode || g_hw_mono_detected;
}

static volatile bool s_testSoundRunning = false;

void AudioEngine::playTestSound(int type) {
    if (s_testSoundRunning) return;
    s_testSoundRunning = true;
    xTaskCreatePinnedToCore([](void* param) {
        int soundType = (int)(intptr_t)param;
        if (soundType == 1) { // Piano C-Major Chord
            AudioEngine::programChange(0, 0); // Piano 1
            AudioEngine::controlChange(0, 10, 64);
            AudioEngine::noteOn(0, 60, 75);
            AudioEngine::noteOn(0, 64, 75);
            AudioEngine::noteOn(0, 67, 75);
            AudioEngine::noteOn(0, 72, 80);
            vTaskDelay(pdMS_TO_TICKS(1500));
            AudioEngine::noteOff(0, 60);
            AudioEngine::noteOff(0, 64);
            AudioEngine::noteOff(0, 67);
            AudioEngine::noteOff(0, 72);
        } else if (soundType == 2) { // Guitar Arpeggio
            AudioEngine::programChange(0, 27); // Clean Guitar
            AudioEngine::controlChange(0, 10, 64);
            AudioEngine::noteOn(0, 52, 80); vTaskDelay(pdMS_TO_TICKS(120));
            AudioEngine::noteOn(0, 55, 80); vTaskDelay(pdMS_TO_TICKS(120));
            AudioEngine::noteOn(0, 59, 80); vTaskDelay(pdMS_TO_TICKS(120));
            AudioEngine::noteOn(0, 64, 85); vTaskDelay(pdMS_TO_TICKS(1200));
            AudioEngine::noteOff(0, 52);
            AudioEngine::noteOff(0, 55);
            AudioEngine::noteOff(0, 59);
            AudioEngine::noteOff(0, 64);
        } else if (soundType == 3) { // Drums
            AudioEngine::programChange(9, 0); // Standard Drums
            AudioEngine::controlChange(9, 10, 64);
            AudioEngine::noteOn(9, 36, 100); vTaskDelay(pdMS_TO_TICKS(200)); // Bass Drum
            AudioEngine::noteOn(9, 38, 95);  vTaskDelay(pdMS_TO_TICKS(200)); // Snare
            AudioEngine::noteOn(9, 42, 85);  vTaskDelay(pdMS_TO_TICKS(200)); // HiHat
            AudioEngine::noteOn(9, 49, 100); vTaskDelay(pdMS_TO_TICKS(1000)); // Crash Cymbal
        } else if (soundType == 4) { // 15-Second 3D Stereo & Spatial Test (Left 5s -> Right 5s -> 3D Spatial 5s)
            AudioEngine::programChange(0, 0);  // Ch 0: Piano
            AudioEngine::programChange(1, 33); // Ch 1: Electric Bass
            AudioEngine::programChange(2, 27); // Ch 2: Clean Guitar
            AudioEngine::programChange(9, 0);  // Ch 9: Standard Drums

            for (int side = 0; side < 3; side++) {
                if (side == 0) { // 100% Left Only
                    DisplayUI::showToast(DisplayUI::isKoreanMode() ? "왼쪽" : "Left", 4500);
                    AudioEngine::controlChange(0, 10, 0);
                    AudioEngine::controlChange(1, 10, 0);
                    AudioEngine::controlChange(2, 10, 0);
                    AudioEngine::controlChange(9, 10, 0);
                } else if (side == 1) { // 100% Right Only
                    DisplayUI::showToast(DisplayUI::isKoreanMode() ? "오른쪽" : "Right", 4500);
                    AudioEngine::controlChange(0, 10, 127);
                    AudioEngine::controlChange(1, 10, 127);
                    AudioEngine::controlChange(2, 10, 127);
                    AudioEngine::controlChange(9, 10, 127);
                } else { // Stereo All Stage
                    DisplayUI::showToast(DisplayUI::isKoreanMode() ? "전체" : "All", 4500);
                    AudioEngine::controlChange(0, 10, 64);
                    AudioEngine::controlChange(1, 10, 64);
                    AudioEngine::controlChange(2, 10, 32);
                    AudioEngine::controlChange(9, 10, 64);
                }

                // Intro Chord
                AudioEngine::noteOn(9, 36, 95); AudioEngine::noteOn(9, 42, 80);
                AudioEngine::noteOn(1, 48, 90);
                AudioEngine::noteOn(2, 60, 75); AudioEngine::noteOn(2, 64, 75); AudioEngine::noteOn(2, 67, 75);
                AudioEngine::noteOn(0, 72, 85);
                vTaskDelay(pdMS_TO_TICKS(250));
                AudioEngine::noteOn(9, 42, 75); AudioEngine::noteOn(0, 74, 85); vTaskDelay(pdMS_TO_TICKS(250));
                AudioEngine::noteOff(1, 48); AudioEngine::noteOn(9, 38, 90); AudioEngine::noteOn(9, 42, 80); AudioEngine::noteOn(1, 52, 90); AudioEngine::noteOn(0, 76, 85); vTaskDelay(pdMS_TO_TICKS(250));
                AudioEngine::noteOn(9, 42, 75); AudioEngine::noteOn(0, 79, 85); vTaskDelay(pdMS_TO_TICKS(250));
                AudioEngine::noteOff(1, 52); AudioEngine::noteOff(2, 60); AudioEngine::noteOff(2, 64); AudioEngine::noteOff(2, 67);
                AudioEngine::noteOn(9, 36, 95); AudioEngine::noteOn(9, 42, 80); AudioEngine::noteOn(1, 43, 90); AudioEngine::noteOn(2, 59, 75); AudioEngine::noteOn(2, 62, 75); AudioEngine::noteOn(2, 67, 75); AudioEngine::noteOn(0, 81, 90); vTaskDelay(pdMS_TO_TICKS(250));
                AudioEngine::noteOn(9, 42, 75); AudioEngine::noteOn(0, 79, 85); vTaskDelay(pdMS_TO_TICKS(250));
                AudioEngine::noteOn(9, 38, 90); AudioEngine::noteOn(9, 42, 80); AudioEngine::noteOn(0, 76, 85); vTaskDelay(pdMS_TO_TICKS(250));
                AudioEngine::noteOn(9, 42, 75); AudioEngine::noteOn(0, 74, 85); vTaskDelay(pdMS_TO_TICKS(250));
                AudioEngine::noteOff(1, 43); AudioEngine::noteOff(2, 59); AudioEngine::noteOff(2, 62); AudioEngine::noteOff(2, 67);
                AudioEngine::noteOn(9, 36, 95); AudioEngine::noteOn(9, 42, 80); AudioEngine::noteOn(1, 41, 90); AudioEngine::noteOn(2, 60, 75); AudioEngine::noteOn(2, 65, 75); AudioEngine::noteOn(2, 69, 75); AudioEngine::noteOn(0, 72, 85); vTaskDelay(pdMS_TO_TICKS(250));
                AudioEngine::noteOn(9, 42, 75); AudioEngine::noteOn(0, 76, 85); vTaskDelay(pdMS_TO_TICKS(250));
                AudioEngine::noteOff(1, 41); AudioEngine::noteOn(9, 38, 90); AudioEngine::noteOn(9, 42, 80); AudioEngine::noteOn(1, 45, 90); AudioEngine::noteOn(0, 79, 85); vTaskDelay(pdMS_TO_TICKS(250));
                AudioEngine::noteOn(9, 42, 75); AudioEngine::noteOn(0, 81, 90); vTaskDelay(pdMS_TO_TICKS(250));
                AudioEngine::noteOff(1, 45); AudioEngine::noteOff(2, 60); AudioEngine::noteOff(2, 65); AudioEngine::noteOff(2, 69);
                AudioEngine::noteOn(9, 36, 95); AudioEngine::noteOn(9, 42, 80); AudioEngine::noteOn(1, 43, 90); AudioEngine::noteOn(2, 59, 75); AudioEngine::noteOn(2, 62, 75); AudioEngine::noteOn(2, 65, 75); AudioEngine::noteOn(2, 67, 75); AudioEngine::noteOn(0, 84, 90); vTaskDelay(pdMS_TO_TICKS(250));
                AudioEngine::noteOn(9, 42, 75); AudioEngine::noteOn(0, 83, 85); vTaskDelay(pdMS_TO_TICKS(250));
                AudioEngine::noteOff(1, 43); AudioEngine::noteOn(9, 38, 90); AudioEngine::noteOn(9, 46, 85); AudioEngine::noteOn(1, 47, 90); AudioEngine::noteOn(0, 86, 90); vTaskDelay(pdMS_TO_TICKS(250));
                AudioEngine::noteOn(9, 38, 85); vTaskDelay(pdMS_TO_TICKS(250));
                AudioEngine::noteOff(1, 47); AudioEngine::noteOff(2, 59); AudioEngine::noteOff(2, 62); AudioEngine::noteOff(2, 65); AudioEngine::noteOff(2, 67);
                AudioEngine::noteOn(9, 36, 100); AudioEngine::noteOn(9, 49, 100); AudioEngine::noteOn(1, 36, 95); AudioEngine::noteOn(2, 60, 80); AudioEngine::noteOn(2, 64, 80); AudioEngine::noteOn(2, 67, 80); AudioEngine::noteOn(0, 60, 85); AudioEngine::noteOn(0, 67, 85); AudioEngine::noteOn(0, 72, 90); AudioEngine::noteOn(0, 84, 95);
                vTaskDelay(pdMS_TO_TICKS(1000));
                AudioEngine::panic();
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            for (int ch = 0; ch < 16; ch++) {
                AudioEngine::controlChange(ch, 10, 64);
            }
        }
        s_testSoundRunning = false;
        vTaskDelete(NULL);
    }, "TestSound", 4096, (void*)(intptr_t)type, 2, NULL, 0);
}
