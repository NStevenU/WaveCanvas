#include "midi_parser.h"
#include "audio_engine.h"
#include "config.h"
#include "display_ui.h"
#include "la32_synth.h"
#include "led_indicator.h"
#include "midi_sequencer.h"
#include <Preferences.h>

// 128종 표준 General MIDI 악기명 테이블
static const char *const GM_INSTRUMENTS[128] = {"Acoustic Piano",
                                                "Bright Piano",
                                                "Electric Grand",
                                                "Honky-tonk",
                                                "Electric Piano 1",
                                                "Electric Piano 2",
                                                "Harpsichord",
                                                "Clavinet",
                                                "Celesta",
                                                "Glockenspiel",
                                                "Music Box",
                                                "Vibraphone",
                                                "Marimba",
                                                "Xylophone",
                                                "Tubular Bells",
                                                "Dulcimer",
                                                "Drawbar Organ",
                                                "Percussive Organ",
                                                "Rock Organ",
                                                "Church Organ",
                                                "Reed Organ",
                                                "Accordion",
                                                "Harmonica",
                                                "Tango Accordion",
                                                "Nylon Guitar",
                                                "Steel Guitar",
                                                "Jazz Guitar",
                                                "Clean Guitar",
                                                "Muted Guitar",
                                                "Overdriven Guitar",
                                                "Distortion Guitar",
                                                "Guitar Harmonics",
                                                "Acoustic Bass",
                                                "Finger Bass",
                                                "Pick Bass",
                                                "Fretless Bass",
                                                "Slap Bass 1",
                                                "Slap Bass 2",
                                                "Synth Bass 1",
                                                "Synth Bass 2",
                                                "Violin",
                                                "Viola",
                                                "Cello",
                                                "Contrabass",
                                                "Tremolo Strings",
                                                "Pizzicato Strings",
                                                "Orchestral Harp",
                                                "Timpani",
                                                "String Ensemble 1",
                                                "String Ensemble 2",
                                                "SynthStrings 1",
                                                "SynthStrings 2",
                                                "Choir Aahs",
                                                "Voice Oohs",
                                                "Synth Voice",
                                                "Orchestra Hit",
                                                "Trumpet",
                                                "Trombone",
                                                "Tuba",
                                                "Muted Trumpet",
                                                "French Horn",
                                                "Brass Section",
                                                "SynthBrass 1",
                                                "SynthBrass 2",
                                                "Soprano Sax",
                                                "Alto Sax",
                                                "Tenor Sax",
                                                "Baritone Sax",
                                                "Oboe",
                                                "English Horn",
                                                "Bassoon",
                                                "Clarinet",
                                                "Piccolo",
                                                "Flute",
                                                "Recorder",
                                                "Pan Flute",
                                                "Blown Bottle",
                                                "Shakuhachi",
                                                "Whistle",
                                                "Ocarina",
                                                "Lead 1 (square)",
                                                "Lead 2 (sawtooth)",
                                                "Lead 3 (calliope)",
                                                "Lead 4 (chiff)",
                                                "Lead 5 (charang)",
                                                "Lead 6 (voice)",
                                                "Lead 7 (fifths)",
                                                "Lead 8 (bass+lead)",
                                                "Pad 1 (new age)",
                                                "Pad 2 (warm)",
                                                "Pad 3 (polysynth)",
                                                "Pad 4 (choir)",
                                                "Pad 5 (bowed)",
                                                "Pad 6 (metallic)",
                                                "Pad 7 (halo)",
                                                "Pad 8 (sweep)",
                                                "FX 1 (rain)",
                                                "FX 2 (soundtrack)",
                                                "FX 3 (crystal)",
                                                "FX 4 (atmosphere)",
                                                "FX 5 (brightness)",
                                                "FX 6 (goblins)",
                                                "FX 7 (echoes)",
                                                "FX 8 (sci-fi)",
                                                "Sitar",
                                                "Banjo",
                                                "Shamisen",
                                                "Koto",
                                                "Kalimba",
                                                "Bag pipe",
                                                "Fiddle",
                                                "Shanai",
                                                "Tinkle Bell",
                                                "Agogo",
                                                "Steel Drums",
                                                "Woodblock",
                                                "Taiko Drum",
                                                "Melodic Tom",
                                                "Synth Drum",
                                                "Reverse Cymbal",
                                                "Guitar Fret Noise",
                                                "Breath Noise",
                                                "Seashore",
                                                "Bird Tweet",
                                                "Telephone Ring",
                                                "Helicopter",
                                                "Applause",
                                                "Gunshot"};

// 128종 Roland MT-32 오리지널 악기명 테이블 (Bank 127)
static const char *const MT32_INSTRUMENTS[128] = {"Acou Piano 1",
                                                  "Acou Piano 2",
                                                  "Acou Piano 3",
                                                  "Elec Piano 1",
                                                  "Elec Piano 2",
                                                  "Elec Piano 3",
                                                  "Elec Piano 4",
                                                  "Honkytonk",
                                                  "Elec Organ 1",
                                                  "Elec Organ 2",
                                                  "Elec Organ 3",
                                                  "Elec Organ 4",
                                                  "Pipe Organ 1",
                                                  "Pipe Organ 2",
                                                  "Pipe Organ 3",
                                                  "Accordion MT",
                                                  "Harpsichord 1",
                                                  "Harpsichord 2",
                                                  "Harpsichord 3",
                                                  "Clavinet 1",
                                                  "Clavinet 2",
                                                  "Clavinet 3",
                                                  "Celesta 1",
                                                  "Celesta 2",
                                                  "Synth Brass 1 MT",
                                                  "Synth Brass 2 MT",
                                                  "Synth Brass 3 MT",
                                                  "Synth Brass 4 MT",
                                                  "Synth Bass 1 MT",
                                                  "Synth Bass 2 MT",
                                                  "Synth Bass 3 MT",
                                                  "Synth Bass 4 MT",
                                                  "Fantasy",
                                                  "Harmo Pan",
                                                  "Chorale",
                                                  "Glasses",
                                                  "Soundtrack MT",
                                                  "Atmosphere MT",
                                                  "Warm Bell",
                                                  "Funny Vox",
                                                  "Echo Bell",
                                                  "Ice Rain MT",
                                                  "Oboe 2001",
                                                  "Echo Pan",
                                                  "Doctor Solo",
                                                  "School Daze",
                                                  "Bell Singer",
                                                  "Square Wave MT",
                                                  "String Section 1",
                                                  "String Section 2",
                                                  "String Section 3",
                                                  "Pizzicato MT",
                                                  "Violin 1",
                                                  "Violin 2",
                                                  "Cello 1",
                                                  "Cello 2",
                                                  "Contrabass MT",
                                                  "Harp 1",
                                                  "Harp 2",
                                                  "Guitar 1",
                                                  "Guitar 2",
                                                  "Elec Guitar 1",
                                                  "Elec Guitar 2",
                                                  "Sitar MT",
                                                  "Acou Bass 1",
                                                  "Acou Bass 2",
                                                  "Elec Bass 1",
                                                  "Elec Bass 2",
                                                  "Slap Bass 1 MT",
                                                  "Slap Bass 2 MT",
                                                  "Fretless 1",
                                                  "Fretless 2",
                                                  "Flute 1",
                                                  "Flute 2",
                                                  "Piccolo 1",
                                                  "Piccolo 2",
                                                  "Recorder MT",
                                                  "Pan Pipes",
                                                  "Sax 1",
                                                  "Sax 2",
                                                  "Sax 3",
                                                  "Sax 4",
                                                  "Clarinet 1",
                                                  "Clarinet 2",
                                                  "Oboe MT",
                                                  "English Horn MT",
                                                  "Bassoon MT",
                                                  "Harmonica MT",
                                                  "Trumpet 1",
                                                  "Trumpet 2",
                                                  "Trombone 1",
                                                  "Trombone 2",
                                                  "French Horn 1",
                                                  "French Horn 2",
                                                  "Tuba MT",
                                                  "Brass Section 1",
                                                  "Brass Section 2",
                                                  "Vibes 1",
                                                  "Vibes 2",
                                                  "Syn Mallet",
                                                  "Windbell",
                                                  "Glock",
                                                  "Tube Bell",
                                                  "Xylophone MT",
                                                  "Marimba MT",
                                                  "Koto MT",
                                                  "Sho",
                                                  "Shakuhachi MT",
                                                  "Whistle 1",
                                                  "Whistle 2",
                                                  "Bottleblow",
                                                  "Breathpipe",
                                                  "Timpani MT",
                                                  "Melodic Tom MT",
                                                  "Deep Snare",
                                                  "Elec Perc 1",
                                                  "Elec Perc 2",
                                                  "Taiko MT",
                                                  "Taiko Rim",
                                                  "Cymbal",
                                                  "Castanets MT",
                                                  "Triangle",
                                                  "Orchestral Hit MT",
                                                  "Telephone MT",
                                                  "Bird Tweet",
                                                  "Big Notes Pad",
                                                  "Water Bell",
                                                  "Jungle Tune"};

uint32_t MIDIParser::currentBaud = DEFAULT_MIDI_BAUD;
ChannelStatus MIDIParser::channels[16];
uint8_t MIDIParser::lastActiveChannel = 0;
uint8_t MIDIParser::runningStatus = 0;
uint8_t MIDIParser::msgBuffer[3] = {0};
uint8_t MIDIParser::msgIndex = 0;
uint8_t MIDIParser::expectedLength = 0;
uint16_t MIDIParser::channelRPN[16] = {
    0x7F7F, 0x7F7F, 0x7F7F, 0x7F7F, 0x7F7F, 0x7F7F, 0x7F7F, 0x7F7F,
    0x7F7F, 0x7F7F, 0x7F7F, 0x7F7F, 0x7F7F, 0x7F7F, 0x7F7F, 0x7F7F};
uint16_t MIDIParser::channelNRPN[16] = {
    0x7F7F, 0x7F7F, 0x7F7F, 0x7F7F, 0x7F7F, 0x7F7F, 0x7F7F, 0x7F7F,
    0x7F7F, 0x7F7F, 0x7F7F, 0x7F7F, 0x7F7F, 0x7F7F, 0x7F7F, 0x7F7F};
static unsigned long lastMidiEventTime = 0;
static SynthMode g_synth_mode = SYNTH_MODE_GM;
static SynthPolicy g_synth_policy = SYNTH_POLICY_AUTO;
static ManualSubMode g_manual_sub_mode = MANUAL_MODE_GM;
static uint16_t s_activeChannelMask = 0;

static HardwareSerial SerialMIDI(2);

SynthMode MIDIParser::getSynthMode() { return g_synth_mode; }

void MIDIParser::setSynthMode(SynthMode mode) {
  if (g_synth_policy == SYNTH_POLICY_MANUAL) {
    mode = (g_manual_sub_mode == MANUAL_MODE_GS) ? SYNTH_MODE_GS :
           (g_manual_sub_mode == MANUAL_MODE_MT32) ? SYNTH_MODE_MT32 : SYNTH_MODE_GM;
  }
  if (g_synth_mode != mode) {
    g_synth_mode = mode;
    AudioEngine::setMasterVolumeDirect(AudioEngine::getMasterVolume());
  }
}

SynthPolicy MIDIParser::getSynthPolicy() { return g_synth_policy; }

void MIDIParser::setSynthPolicy(SynthPolicy policy, bool save) {
  g_synth_policy = policy;
  if (g_synth_policy == SYNTH_POLICY_MANUAL) {
    setManualSubMode(g_manual_sub_mode, false);
  }
  if (save) {
    Preferences prefs;
    if (prefs.begin("synth_cfg", false)) {
      prefs.putUChar("policy", (uint8_t)g_synth_policy);
      prefs.end();
    }
  }
}

ManualSubMode MIDIParser::getManualSubMode() { return g_manual_sub_mode; }

void MIDIParser::setManualSubMode(ManualSubMode mode, bool save) {
  g_manual_sub_mode = mode;
  if (g_synth_policy == SYNTH_POLICY_MANUAL) {
    switch (g_manual_sub_mode) {
    case MANUAL_MODE_GS:
      setSynthMode(SYNTH_MODE_GS);
      AudioEngine::applyGSModeDirect();
      break;
    case MANUAL_MODE_MT32:
      setSynthMode(SYNTH_MODE_MT32);
      AudioEngine::applyMT32ModeDirect();
      break;
    case MANUAL_MODE_GM:
    default:
      setSynthMode(SYNTH_MODE_GM);
      LA32SynthEngine::reset();
      AudioEngine::applyGMModeDirect();
      break;
    }
    AudioEngine::panicDirect();
  }
  if (save) {
    Preferences prefs;
    if (prefs.begin("synth_cfg", false)) {
      prefs.putUChar("manual_sub", (uint8_t)g_manual_sub_mode);
      prefs.end();
    }
  }
}

void MIDIParser::cycleManualSubMode() {
  if (g_synth_policy != SYNTH_POLICY_MANUAL) {
    setSynthPolicy(SYNTH_POLICY_MANUAL, true);
    return;
  }
  uint8_t next = ((uint8_t)g_manual_sub_mode + 1) % 3;
  setManualSubMode((ManualSubMode)next, true);
}

const char *MIDIParser::getSynthModeString() {
  switch (g_synth_mode) {
  case SYNTH_MODE_GS:
    return "[GS]";
  case SYNTH_MODE_MT32:
    return "[MT-32]";
  case SYNTH_MODE_GM:
  default:
    return "[GM]";
  }
}

const char *MIDIParser::getIndicatorString() {
  bool isManual = (g_synth_policy == SYNTH_POLICY_MANUAL);
  switch (g_synth_mode) {
  case SYNTH_MODE_GS:
    return isManual ? "[M: GS]" : "[A: GS]";
  case SYNTH_MODE_MT32:
    return isManual ? "[M: MT-32]" : "[A: MT-32]";
  case SYNTH_MODE_GM:
  default:
    return isManual ? "[M: GM]" : "[A: GM]";
  }
}

static void serialMidiTask(void* pv) {
    uint8_t rxBuf[128];
    while (true) {
        int avail = SerialMIDI.available();
        if (avail > 0) {
            if (MIDISequencer::getState() == SEQ_PLAYING) {
                MIDISequencer::stop();
                DisplayUI::onExternalMIDIActivity();
            }
            int toRead = (avail > (int)sizeof(rxBuf)) ? (int)sizeof(rxBuf) : avail;
            int count = SerialMIDI.readBytes(rxBuf, toRead);
            for (int i = 0; i < count; i++) {
                MIDIParser::parseByte(rxBuf[i]);
            }
        }
        MIDIParser::update(); // 무신호 감지 및 VU 미터 처리
        vTaskDelay(pdMS_TO_TICKS(1)); // Core 0 Watchdog 및 DisplayTask에 확실한 CPU 양보 (재부팅 원천 차단)
    }
}

void MIDIParser::begin(uint32_t baudRate) {
  currentBaud = baudRate;
  SerialMIDI.setRxBufferSize(4096);
  SerialMIDI.begin(currentBaud, SERIAL_8N1, PIN_MIDI_RX, PIN_MIDI_TX);

  // 채널 상태 초기화
  for (int i = 0; i < 16; i++) {
    channels[i].program = 0;
    channels[i].volume = 100;
    channels[i].expression = 127;
    channels[i].vuLevel = 0;
    channels[i].lastNoteTime = 0;
    channelRPN[i] = 0x7F7F; // RPN Null
    channelNRPN[i] = 0x7F7F; // NRPN Null
  }
  channels[9].program = 0; // Drum Kit
  lastMidiEventTime = 0;

  // NVS 설정 복원
  Preferences prefs;
  if (prefs.begin("synth_cfg", true)) {
    g_synth_policy = (SynthPolicy)prefs.getUChar("policy", (uint8_t)SYNTH_POLICY_AUTO);
    g_manual_sub_mode = (ManualSubMode)prefs.getUChar("manual_sub", (uint8_t)MANUAL_MODE_GM);
    prefs.end();
  }
  if (g_synth_policy == SYNTH_POLICY_MANUAL) {
    setManualSubMode(g_manual_sub_mode, false);
  }

  // Core 0에서 화면/네트워크(1~2)보다 높은 우선순위 4로 생성
  TaskHandle_t hMidi = NULL;
  xTaskCreatePinnedToCore(serialMidiTask, "SerialMIDI", 4096, NULL, 4, &hMidi, 0);
  DEBUG_REG_MIDI_TASK(hMidi);
}

bool MIDIParser::isMIDIActive() {
  return (lastMidiEventTime > 0 && (millis() - lastMidiEventTime < 1500));
}

void MIDIParser::setBaudRate(uint32_t baud) {
  currentBaud = baud;
  SerialMIDI.updateBaudRate(currentBaud);
}

uint32_t MIDIParser::getBaudRate() { return currentBaud; }

const ChannelStatus &MIDIParser::getChannelStatus(uint8_t channel) {
  return channels[channel & 0x0F];
}

void MIDIParser::setChannelProgram(uint8_t ch, uint8_t prog) {
  if (ch < 16)
    channels[ch].program = prog;
}

void MIDIParser::setChannelVolume(uint8_t ch, uint8_t vol) {
  if (ch < 16)
    channels[ch].volume = vol;
}

void MIDIParser::setChannelExpression(uint8_t ch, uint8_t expr) {
  if (ch < 16)
    channels[ch].expression = expr;
}

void MIDIParser::setChannelVU(uint8_t ch, uint8_t vu) {
  if (ch < 16)
    channels[ch].vuLevel = vu;
}

void MIDIParser::setChannelLastNoteTime(uint8_t ch, uint32_t t) {
  if (ch < 16)
    channels[ch].lastNoteTime = t;
}

void MIDIParser::resetChannelStatus(uint8_t ch) {
  if (ch < 16) {
    channels[ch].program = 0;
    channels[ch].volume = 100;
    channels[ch].expression = 127;
    channels[ch].vuLevel = 0;
    channels[ch].lastNoteTime = 0;
    channelRPN[ch] = 0x7F7F;
    channelNRPN[ch] = 0x7F7F;
  }
}

const char *MIDIParser::getInstrumentName(uint8_t program, bool isDrum) {
  if (g_synth_mode == SYNTH_MODE_MT32) {
    if (isDrum) {
      return "MT-32 Rhythm Set";
    }
    return MT32_INSTRUMENTS[program & 0x7F];
  }

  if (isDrum) {
    switch (program) {
    case 8:
      return "Room Drum Set";
    case 16:
      return "Power Drum Set";
    case 24:
      return "Electronic Set";
    case 25:
      return "TR-808 Drum Set";
    case 32:
      return "Jazz Drum Set";
    case 40:
      return "Brush Drum Set";
    case 48:
      return "Orchestra Set";
    case 56:
      return "SFX Drum Set";
    case 127:
      return "CM-64/32 Set";
    case 0:
    default:
      return "Standard Drum Set";
    }
  }
  return GM_INSTRUMENTS[program & 0x7F];
}

uint8_t MIDIParser::getLastActiveChannel() { return lastActiveChannel; }

void MIDIParser::setLastActiveChannel(uint8_t ch) {
  if (ch < 16) {
    lastActiveChannel = ch;
  }
}

static uint32_t s_lastGSResetTime = 0;
static uint32_t s_lastGlobalNoteTime = 0;
static bool s_sessionActive = false;   // 첫 Note On 발음 이후 true로 잠금(Lock)
static bool s_hasPlayedNotes = false;  // 실제 음표(Note On)가 연주되었는지 여부
static bool s_hasGSFingerprint = false; // 프리롤에서 GS 지문 감지 여부
static bool s_hasMT32Fingerprint = false; // 프리롤에서 MT-32 지문 감지 여부

// 채널별 설정 수신 추적 마스크 (16개 채널)
static bool s_chExprSet[16] = {false};
static bool s_chVolumeSet[16] = {false};
static bool s_chBankSet[16] = {false};
static bool s_chProgSet[16] = {false};
static bool s_chPanSet[16] = {false};

void MIDIParser::processCompleteMessage() {
  DEBUG_RECORD_MIDI();
  uint32_t now = millis();
  uint32_t idleMs = (lastMidiEventTime > 0) ? (now - lastMidiEventTime) : 999999;

  uint8_t status = msgBuffer[0] & 0xF0;
  uint8_t channel = msgBuffer[0] & 0x0F;

  // [새 곡 시작 감지: 이전 곡이 실제로 연주된 후 4.0초 이상 무음이 경과하고 새 이벤트가 들어올 때만 세션 윈도우 개방!]
  // 🌟 아직 첫 음표가 나오지 않은 프리롤 구간(!s_hasPlayedNotes)에서는 인트로 무음이 6초 이상 발생하더라도 프리롤 설정을 절대 리셋하지 않음!
  if (lastMidiEventTime == 0 || (s_hasPlayedNotes && idleMs >= 4000)) {
    s_hasPlayedNotes = false;   // 아직 음표는 연주되지 않음!
    s_sessionActive = false;    // 새 곡을 위한 프리롤 감지 윈도우 개방!
    s_hasGSFingerprint = false;
    s_hasMT32Fingerprint = false;
    s_activeChannelMask = 0;

    // 파라미터 수신 마스크 초기화
    for (int c = 0; c < 16; c++) {
      s_chExprSet[c] = false;
      s_chVolumeSet[c] = false;
      s_chBankSet[c] = false;
      s_chProgSet[c] = false;
      s_chPanSet[c] = false;
      channelRPN[c] = 0x7F7F;
      channelNRPN[c] = 0x7F7F;
    }

    // 이전 곡의 잔여 발음 음표만 안전하게 정지 (모드/파라미터는 프리롤 수집을 위해 보존!)
    if (g_synth_mode == SYNTH_MODE_MT32) {
      for (int ch = 0; ch < 16; ch++) {
        LA32SynthEngine::allNotesOff(ch);
      }
    }
  }

  lastMidiEventTime = now;
  DisplayUI::onExternalMIDIActivity();

  switch (status) {
  case 0x90: { // Note On
    uint8_t note = msgBuffer[1];
    uint8_t vel = msgBuffer[2];
    if (vel > 0) {
      s_lastGlobalNoteTime = now;
      // 실시간 UART는 프리롤의 CC/볼륨/팬 초기화로 채널이 오염되지 않도록,
      // 실제 음표가 발생한 채널만 활성 마스크에 반영한다.
      s_activeChannelMask |= (1 << channel);

      // 🌟 [원자적 모드 확정 및 미설정 파라미터 선별 플러시 (Atomic Staging & Untouched Flush)]
      // 첫 음표가 발음되기 직전 딱 1회 실행!
      if (!s_sessionActive) {
        if (g_synth_policy == SYNTH_POLICY_MANUAL) {
          // 🔒 수동 모드 고정: 사용자가 지정한 서브 모드로 강제 확정 (자동 전환 원천 차단)
          switch (g_manual_sub_mode) {
          case MANUAL_MODE_GS:
            MIDIParser::setSynthMode(SYNTH_MODE_GS);
            break;
          case MANUAL_MODE_MT32:
            MIDIParser::setSynthMode(SYNTH_MODE_MT32);
            break;
          case MANUAL_MODE_GM:
          default:
            MIDIParser::setSynthMode(SYNTH_MODE_GM);
            break;
          }
        } else {
          // 🌐 스마트 자동 모드:
          // 1. MT-32 모드: 이번 곡에서 MT-32 SysEx가 감지되었거나,
          //    직전이 MT-32 모드이면서 이번 곡에 Ch 0 및 Ch 10~15가 전혀 없고 오직 MT-32 채널(1~9)만 사용될 때!
          bool isPureMT32 = (s_hasMT32Fingerprint) ||
                            (g_synth_mode == SYNTH_MODE_MT32 && (s_activeChannelMask & 0xFC01) == 0 && channel != 0 && channel < 10);
          if (isPureMT32) {
            MIDIParser::setSynthMode(SYNTH_MODE_MT32);
          }
          // 2. GS 모드 확정 시 (이번 곡에서 실제 GS 지문이 감지된 경우)
          else if (s_hasGSFingerprint) {
            MIDIParser::setSynthMode(SYNTH_MODE_GS);
            for (int ch = 0; ch < 16; ch++) {
              if (!s_chExprSet[ch]) {
                channels[ch].expression = 127;
                AudioEngine::controlChange(ch, 11, 127);
              }
              if (!s_chVolumeSet[ch]) {
                channels[ch].volume = 100;
                AudioEngine::controlChange(ch, 7, 100);
              }
              if (!s_chBankSet[ch]) {
                if (ch != 9) AudioEngine::setBank(ch, 0);
              }
            }
          }
          // 3. 순수 GM 모드 확정 시 (PM2 GM, All For You, 동방 등: MT-32 -> GM 및 GS -> GM 완벽 전환!)
          else {
            MIDIParser::setSynthMode(SYNTH_MODE_GM);
            LA32SynthEngine::reset();
            AudioEngine::resetMT32FilterDirect();
            AudioEngine::resetDrumKeyParamsDirect();       // 🌟 드럼 키 피치/팬/볼륨/컷오프 100% 클린 리셋!
            AudioEngine::setGSMasterEQDirect(0, 0, 0, 0); // 🌟 마스터 EQ 0dB 플랫 복구!
            AudioEngine::setReverbMacroDirect(2);         // 🌟 Room 3 (GM 기본 리버브 복구)
            AudioEngine::setChorusMacroDirect(2);         // 🌟 Chorus 3 (기본 코러스 복구)
            
            // 새 곡에서 건드리지 않은 채널의 파라미터만 선별 기본값(Default) 100% 완전 복구!
            for (int ch = 0; ch < 16; ch++) {
              AudioEngine::controlChange(ch, 121, 0);

              if (s_chExprSet[ch]) {
                AudioEngine::controlChange(ch, 11, channels[ch].expression);
              } else {
                channels[ch].expression = 127;
                AudioEngine::controlChange(ch, 11, 127);
              }

              if (s_chVolumeSet[ch]) {
                AudioEngine::controlChange(ch, 7, channels[ch].volume);
              } else {
                channels[ch].volume = 100;
                AudioEngine::controlChange(ch, 7, 100);
              }

              if (s_chPanSet[ch]) {
                // 이미 수신된 Pan 유지
              } else {
                AudioEngine::controlChange(ch, 10, 64);
              }

              if (s_chBankSet[ch]) {
                // 이미 수신된 Bank 유지
              } else {
                if (ch != 9) AudioEngine::setBank(ch, 0);
              }

              if (s_chProgSet[ch]) {
                AudioEngine::programChange(ch, channels[ch].program);
              } else {
                if (ch != 9) AudioEngine::programChange(ch, 0);
              }

              AudioEngine::setPitchRange(ch, 2.0f);
            }
          }
        }
        s_sessionActive = true; // 세션 모드 잠금(Lock)! 연주 도중 핑퐁 차단
      }

      s_hasPlayedNotes = true; // 실제 음표 연주 시작 확정!
      AudioEngine::noteOn(channel, note, vel);
      DisplayUI::wakeup();
      channels[channel].vuLevel = (uint8_t)(vel / 8); // 벨로시티 높이 (2 ~ 16)
      if (channels[channel].vuLevel < 2)
        channels[channel].vuLevel = 2;
      channels[channel].lastNoteTime = millis();
      lastActiveChannel = channel;
      LEDIndicator::pulseMIDI();
    } else {
      AudioEngine::noteOff(channel, note);
      channels[channel].vuLevel = 0; // 즉시 바닥선 복귀
    }
    break;
  }
  case 0x80: { // Note Off
    uint8_t note = msgBuffer[1];
    AudioEngine::noteOff(channel, note);
    channels[channel].vuLevel = 0; // 즉시 바닥선 복귀
    break;
  }
  case 0xC0: { // Program Change (악기 변경)
    uint8_t prog = msgBuffer[1];
    channels[channel].program = prog;
    s_chProgSet[channel] = true;
    AudioEngine::programChange(channel, prog); // TSF Mutex 보호 적용
    lastActiveChannel = channel;
    break;
  }
  case 0xB0: { // Control Change
    uint8_t ctrl = msgBuffer[1];
    uint8_t val = msgBuffer[2];
    
    // Bank Select MSB (CC 0)
    if (ctrl == 0) {
      s_chBankSet[channel] = true;
      if (g_synth_policy == SYNTH_POLICY_MANUAL && g_manual_sub_mode == MANUAL_MODE_GM && channel != 9) {
        val = 0; // 수동 GM 모드일 때는 Bank 0 고정
      }
      AudioEngine::setBank(channel, val); // TSF Mutex 보호 적용
      
      // [프리롤 스마트 지문 감지]
      if (!s_sessionActive && g_synth_policy == SYNTH_POLICY_AUTO) {
        if ((channel != 9 && val > 0 && val < 127) || (channel == 9 && val == 127)) {
          s_hasGSFingerprint = true;
          if (g_synth_mode != SYNTH_MODE_GS) {
            MIDIParser::setSynthMode(SYNTH_MODE_GS);
          }
        }
      }
    }

    if (ctrl == 7) {
      channels[channel].volume = val;
      s_chVolumeSet[channel] = true;
    }
    if (ctrl == 11) {
      channels[channel].expression = val;
      s_chExprSet[channel] = true;
    }
    if (ctrl == 10) {
      s_chPanSet[channel] = true;
    }

    // NRPN / RPN 등록 (8비트 MSB/LSB 결합)
    if (ctrl == 99) { // NRPN MSB
      channelNRPN[channel] = (channelNRPN[channel] & 0x00FF) | ((uint16_t)val << 8);
      channelRPN[channel] = 0x7F7F;

      // [프리롤 GS 지문 감지] 곡 시작 전 GS Drum Key NRPN (0x18, 0x1A, 0x1C, 0x1D) 또는 TVF/ENV NRPN (0x01) 수신 시!
      if (!s_sessionActive) {
        if (val == 0x01 || val == 0x18 || val == 0x1A || val == 0x1C || val == 0x1D) {
          s_hasGSFingerprint = true;
          if (g_synth_mode != SYNTH_MODE_GS) {
            MIDIParser::setSynthMode(SYNTH_MODE_GS);
          }
        }
      }
    }
    if (ctrl == 98) { // NRPN LSB
      channelNRPN[channel] = (channelNRPN[channel] & 0xFF00) | (val & 0x7F);
      channelRPN[channel] = 0x7F7F;
    }
    if (ctrl == 101) { // RPN MSB
      channelRPN[channel] = (channelRPN[channel] & 0x00FF) | ((uint16_t)val << 8);
      channelNRPN[channel] = 0x7F7F;
    }
    if (ctrl == 100) { // RPN LSB
      channelRPN[channel] = (channelRPN[channel] & 0xFF00) | (val & 0x7F);
      channelNRPN[channel] = 0x7F7F;
    }

    if (ctrl == 6) { // Data Entry MSB
      if (channelRPN[channel] == 0x0000) { // Pitch Bend Sensitivity (RPN 00 00)
        uint8_t range = (val > 24) ? 24 : val;
        AudioEngine::setPitchRange(channel, (float)range);
      } else if (channelNRPN[channel] == 0x0108) { // Vibrato Rate
        AudioEngine::controlChange(channel, 76, val);
      } else if (channelNRPN[channel] == 0x0109) { // Vibrato Depth
        AudioEngine::controlChange(channel, 77, val);
      } else if (channelNRPN[channel] == 0x010A) { // Vibrato Delay
        AudioEngine::controlChange(channel, 78, val);
      } else if (channelNRPN[channel] == 0x0120) { // TVF Cutoff Frequency
        AudioEngine::controlChange(channel, 74, val);
      } else if (channelNRPN[channel] == 0x0121) { // TVF Resonance
        AudioEngine::controlChange(channel, 71, val);
      } else if (channelNRPN[channel] == 0x0163) { // Envelope Attack Time
        AudioEngine::controlChange(channel, 73, val);
      } else if (channelNRPN[channel] == 0x0164) { // Envelope Decay Time
        AudioEngine::controlChange(channel, 75, val);
      } else if (channelNRPN[channel] == 0x0166) { // Envelope Release Time
        AudioEngine::controlChange(channel, 72, val);
      } else if ((channelNRPN[channel] >> 8) == 0x18) { // Drum Key Pitch Coarse
        uint8_t dKey = channelNRPN[channel] & 0x7F;
        AudioEngine::setDrumKeyPitchDirect(dKey, (int8_t)val - 64);
      } else if ((channelNRPN[channel] >> 8) == 0x1A) { // Drum Key TVF Cutoff
        uint8_t dKey = channelNRPN[channel] & 0x7F;
        AudioEngine::setDrumKeyCutoffDirect(dKey, (int8_t)val - 64);
      } else if ((channelNRPN[channel] >> 8) == 0x1C) { // Drum Key Level
        uint8_t dKey = channelNRPN[channel] & 0x7F;
        AudioEngine::setDrumKeyLevelDirect(dKey, val);
      } else if ((channelNRPN[channel] >> 8) == 0x1D) { // Drum Key Panpot
        uint8_t dKey = channelNRPN[channel] & 0x7F;
        AudioEngine::setDrumKeyPanDirect(dKey, val);
      }
    }
    if (ctrl == 121) {
      channelRPN[channel] = 0x7F7F;
      channelNRPN[channel] = 0x7F7F;
    }
    if (ctrl == 120 || ctrl == 123) {
      channels[channel].vuLevel = 0;
    }
    if (ctrl == 126) AudioEngine::setChannelMono(channel, true);
    if (ctrl == 127) AudioEngine::setChannelMono(channel, false);
    AudioEngine::controlChange(channel, ctrl, val);
    break;
  }
  case 0xD0: { // Channel Pressure
    AudioEngine::controlChange(channel, 1, msgBuffer[1] / 2);
    break;
  }
  case 0xA0: { // Polyphonic Aftertouch
    AudioEngine::controlChange(channel, 1, msgBuffer[2] / 2);
    break;
  }
  case 0xE0: { // Pitch Bend
    uint16_t bend = (uint16_t)msgBuffer[1] | ((uint16_t)msgBuffer[2] << 7);
    AudioEngine::pitchBend(channel, bend);
    break;
  }
  }
}

static inline bool isValidRolandChecksum(const uint8_t *data, size_t len) {
  if (len < 2)
    return true;
  uint32_t sum = 0;
  for (size_t i = 0; i < len - 1; i++) {
    sum += data[i];
  }
  uint8_t expectedChk = (128 - (sum % 128)) & 0x7F;
  return (data[len - 1] == expectedChk);
}

static uint8_t sysexBuf[512];
static uint16_t sysexLen = 0;
static bool inSysEx = false;
static bool sysexOverflow = false;

void MIDIParser::parseByte(uint8_t b) {
  // Realtime 메시지 (0xF8 ~ 0xFF)는 버퍼 방해 없이 무시/통과
  if (b >= 0xF8) {
    if (b == 0xFF)
      AudioEngine::systemReset(); // Reset
    return;
  }

  // System Common 메시지 (0xF1 ~ 0xF6)는 Running Status를 리셋하고 무시
  if (b >= 0xF1 && b <= 0xF6) {
    runningStatus = 0;
    msgIndex = 0;
    return;
  }

  // 1. SysEx 수신 중이거나 0xF0가 들어온 경우
  if (inSysEx || b == 0xF0) {
    parseSysExByte(b);
    // SysEx를 끝내면서 들어온 일반 상태 바이트(0x80~0xEF)는 아래로 내려보내 정상 처리하고,
    // SysEx가 계속 진행 중이거나 F0/F7 바이트 자체인 경우에만 즉시 리턴
    if (inSysEx || b == 0xF0 || b == 0xF7)
      return;
  }

  // 2. F0 없이 들어오는 Roland Raw SysEx Escape (0x41 0x10 0x16 ...) 감지 (가상 0xF0 전치로 오프셋 일치)
  if (msgIndex == 0 && runningStatus == 0 && b == 0x41) {
    inSysEx = true;
    sysexOverflow = false;
    sysexLen = 0;
    sysexBuf[sysexLen++] = 0xF0;
    sysexBuf[sysexLen++] = 0x41;
    lastMidiEventTime = millis();
    return;
  }

  // 3. Status Byte (0x80 ~ 0xEF)
  if (b & 0x80) {
    runningStatus = b;
    msgBuffer[0] = b;
    msgIndex = 1;
    uint8_t type = b & 0xF0;
    if (type == 0xC0 || type == 0xD0) {
      expectedLength = 2;
    } else {
      expectedLength = 3;
    }
    return;
  }

  // 4. Data Byte (Running Status 지원)
  if (msgIndex == 0 && runningStatus != 0) {
    msgBuffer[0] = runningStatus;
    msgIndex = 1;
    uint8_t type = runningStatus & 0xF0;
    expectedLength = (type == 0xC0 || type == 0xD0) ? 2 : 3;
  }

  if (msgIndex > 0) {
    msgBuffer[msgIndex++] = b;
    if (msgIndex >= expectedLength) {
      processCompleteMessage();
      msgIndex = 0; // 메시지 완성 후 리셋 (Running Status는 유지)
    }
  }
}

void MIDIParser::parseSysExByte(uint8_t b) {
  if (!inSysEx) {
    if (b == 0xF0) {
      inSysEx = true;
      sysexOverflow = false;
      sysexLen = 0;
      sysexBuf[sysexLen++] = b;
      lastMidiEventTime = millis(); // SysEx 수신 시 타이머 갱신
    }
  } else {
    if (b == 0xF7 || (b & 0x80)) {
      inSysEx = false;
      lastMidiEventTime = millis();
      DEBUG_RECORD_SYSEX(sysexLen, sysexOverflow);
      if (sysexOverflow) {
        sysexLen = 0;
        sysexOverflow = false;
        return; // 버퍼 초과로 잘린 손상 데이터는 파싱 건너뜀
      }
      if (b == 0xF7) {
        sysexBuf[sysexLen++] = b;
      }

      // SysEx 분석
      if (sysexLen >= 20 && sysexBuf[1] == 0x41 && sysexBuf[3] == 0x16 &&
          sysexBuf[4] == 0x12 && sysexBuf[5] == 0x20 && sysexBuf[6] == 0x00 &&
          sysexBuf[7] == 0x00) {
        // Roland MT-32 LCD Text Display SysEx
        if (isValidRolandChecksum(&sysexBuf[5], sysexLen - 6)) {
          char lcdText[24] = {0};
          for (int i = 0; i < 20 && (8 + i) < sysexLen - 2; i++) {
            char c = (char)sysexBuf[8 + i];
            lcdText[i] = (c >= 32 && c <= 126) ? c : ' ';
          }
          DisplayUI::showToast(lcdText, 3500);
        }
      } else if (sysexLen >= 11 && sysexBuf[1] == 0x41 && sysexBuf[3] == 0x16 &&
                 sysexBuf[4] == 0x12 && sysexBuf[5] == 0x10 &&
                 sysexBuf[6] == 0x00 && sysexBuf[7] == 0x01) {
        // Roland MT-32 Reverb Parameter SysEx
        s_hasMT32Fingerprint = true;
        MIDIParser::setSynthMode(SYNTH_MODE_MT32);
        AudioEngine::setMT32ReverbDirect(
            sysexBuf[8], sysexBuf[9], (sysexLen >= 13 ? sysexBuf[10] : 64));
      } else if (sysexLen >= 14 && sysexBuf[1] == 0x41 && sysexBuf[3] == 0x16 &&
                 sysexBuf[4] == 0x12 &&
                 (sysexBuf[5] == 0x04 || sysexBuf[5] == 0x08)) {
        // Roland MT-32 Timbre Temp / User Timbre Memory Dump (14 ~ 256바이트 파셜 음색 덤프)
        s_hasMT32Fingerprint = true;
        MIDIParser::setSynthMode(SYNTH_MODE_MT32);
        AudioEngine::applyMT32ModeDirect();

        uint8_t targetPart = sysexBuf[6] & 0x07; // Part 1~8 -> Channel 1~8 (Ch 2~9)
        uint8_t targetCh = targetPart + 1;       // MT-32 Part 1 = MIDI Ch 2(index 1)
        SemaphoreHandle_t mutex = AudioEngine::getMutex();
        if (mutex)
          xSemaphoreTake(mutex, pdMS_TO_TICKS(50));
        LA32SynthEngine::setCustomTimbre(targetCh, &sysexBuf[8],
                                         sysexLen - 9);
        if (mutex)
          xSemaphoreGive(mutex);
      } else if (sysexLen >= 14 && sysexLen < 60 && sysexBuf[1] == 0x41 && sysexBuf[3] == 0x16 &&
                 sysexBuf[4] == 0x12 && (sysexBuf[5] == 0x03 || sysexBuf[5] == 0x05) &&
                 (sysexBuf[7] == 0x00 || sysexBuf[7] == 0x02)) {
        // Roland MT-32 Patch Temp Memory (Key Shift, Fine Tune, Bender Range)
        s_hasMT32Fingerprint = true;
        MIDIParser::setSynthMode(SYNTH_MODE_MT32);
        AudioEngine::applyMT32ModeDirect();

        uint8_t targetPart = sysexBuf[6] & 0x07;
        uint8_t targetCh = targetPart + 1;
        if (sysexLen >= 16) {
          int8_t keyShift = (int8_t)sysexBuf[10] - 24;
          float fineTune =
              ((float)sysexBuf[11] - 50.0f) / 50.0f;
          uint8_t bender = sysexBuf[12];
          AudioEngine::setChannelKeyShift(targetCh, keyShift);
          AudioEngine::setChannelTuningOffset(targetCh, fineTune);
          if (bender > 0 && bender <= 24)
            AudioEngine::setPitchRange(targetCh, (float)bender);
        }
      } else if (sysexLen >= 11 && sysexBuf[1] == 0x41 && sysexBuf[3] == 0x16 &&
                 sysexBuf[4] == 0x12 && sysexBuf[5] == 0x03 &&
                 sysexBuf[6] == 0x01 && sysexBuf[7] == 0x10) {
        // Roland MT-32 Rhythm Temp
        s_hasMT32Fingerprint = true;
        uint8_t key = sysexBuf[8];
        uint8_t level = (sysexLen >= 13) ? sysexBuf[10] : 100;
        uint8_t pan = (sysexLen >= 14) ? sysexBuf[11] : 7;
        AudioEngine::setDrumKeyLevelDirect(key, level);
        AudioEngine::setDrumKeyPanDirect(key, pan);
      } else if (sysexLen >= 6 && sysexBuf[1] == 0x7E && sysexBuf[3] == 0x09 &&
                 (sysexBuf[4] == 0x01 || sysexBuf[4] == 0x03)) {
        // GM1 / GM2 System On -> GM 모드로 통일
        if (g_synth_policy == SYNTH_POLICY_AUTO) {
          s_hasMT32Fingerprint = false;
          s_hasGSFingerprint = false;
          MIDIParser::setSynthMode(SYNTH_MODE_GM);
          AudioEngine::applyGMModeDirect();
          AudioEngine::systemReset();
          clearAllVU();
        }
      } else if (sysexLen >= 5 && sysexBuf[1] == 0x41 && sysexBuf[3] == 0x16) {
        // Roland MT-32 SysEx (F0 41 <dev> 16 ...)
        s_hasMT32Fingerprint = true;
        if (g_synth_policy == SYNTH_POLICY_AUTO && g_synth_mode != SYNTH_MODE_MT32) {
          MIDIParser::setSynthMode(SYNTH_MODE_MT32);
          AudioEngine::applyMT32ModeDirect();
          clearAllVU();
        }
      } else if (sysexLen >= 11 && sysexBuf[1] == 0x41 && sysexBuf[3] == 0x42 &&
                 sysexBuf[4] == 0x12 && sysexBuf[5] == 0x40 &&
                 sysexBuf[6] == 0x00 && sysexBuf[7] == 0x7F) {
        // Roland GS Reset
        s_hasGSFingerprint = true;
        s_lastGSResetTime = millis();
        if (g_synth_policy == SYNTH_POLICY_AUTO) {
          MIDIParser::setSynthMode(SYNTH_MODE_GS);
          AudioEngine::applyGSModeDirect();
          AudioEngine::systemReset();
          clearAllVU();
        }
      } else if (sysexLen >= 12 && sysexBuf[1] == 0x41 && sysexBuf[3] == 0x42 &&
                 sysexBuf[4] == 0x12 && sysexBuf[5] == 0x40 &&
                 sysexBuf[6] == 0x02 && sysexBuf[7] == 0x00) {
        // Roland GS Master EQ
        int8_t lowGain = (int8_t)sysexBuf[9] - 64;
        int8_t highGain = (int8_t)sysexBuf[11] - 64;
        AudioEngine::setGSMasterEQ(sysexBuf[8], lowGain, sysexBuf[10],
                                   highGain);
      } else if (sysexLen >= 11 && sysexBuf[1] == 0x41 && sysexBuf[3] == 0x42 &&
                 sysexBuf[4] == 0x12 && sysexBuf[5] == 0x40 &&
                 sysexBuf[6] == 0x01 && sysexBuf[7] == 0x00) {
        // Roland GS SC-55 LCD Text Display SysEx
        char lcdText[36] = {0};
        int maxChars = sysexLen - 10;
        if (maxChars > 32)
          maxChars = 32;
        for (int i = 0; i < maxChars; i++) {
          char c = (char)sysexBuf[8 + i];
          lcdText[i] = (c >= 32 && c <= 126) ? c : ' ';
        }
        DisplayUI::showToast(lcdText, 3500);
      } else if (sysexLen >= 9 && sysexBuf[1] == 0x41 && sysexBuf[3] == 0x42 &&
                 sysexBuf[4] == 0x12 && sysexBuf[5] == 0x40 &&
                 sysexBuf[6] == 0x01 && sysexBuf[7] == 0x30) {
        // Roland GS Reverb Parameters
        AudioEngine::setReverbMacroDirect(sysexBuf[8]);
        if (sysexLen >= 14) {
          AudioEngine::setGSReverbParamsDirect(sysexBuf[8], sysexBuf[10],
                                               sysexBuf[11], sysexBuf[12]);
        }
      } else if (sysexLen >= 9 && sysexBuf[1] == 0x41 && sysexBuf[3] == 0x42 &&
                 sysexBuf[4] == 0x12 && sysexBuf[5] == 0x40 &&
                 sysexBuf[6] == 0x01 && sysexBuf[7] == 0x38) {
        // Roland GS Chorus Parameters
        AudioEngine::setChorusMacroDirect(sysexBuf[8]);
        if (sysexLen >= 15) {
          AudioEngine::setGSChorusParamsDirect(sysexBuf[10], sysexBuf[11],
                                               sysexBuf[12], sysexBuf[13],
                                               sysexBuf[14]);
        }
      } else if (sysexLen >= 9 && sysexBuf[1] == 0x41 && sysexBuf[3] == 0x42 &&
                 sysexBuf[4] == 0x12 && sysexBuf[5] == 0x40 &&
                 (sysexBuf[6] & 0xF0) == 0x10) {
        // Roland GS Part Parameters
        MIDIParser::setSynthMode(SYNTH_MODE_GS);
        uint8_t part = sysexBuf[6] & 0x0F;
        uint8_t targetCh =
            (part == 0) ? 9 : ((part <= 9) ? (part - 1) : part);
        if (sysexBuf[7] == 0x08) {
          AudioEngine::controlChange(targetCh, 76, sysexBuf[8]);
        } else if (sysexBuf[7] == 0x09) {
          AudioEngine::controlChange(targetCh, 77, sysexBuf[8]);
        } else if (sysexBuf[7] == 0x0A) {
          AudioEngine::controlChange(targetCh, 78, sysexBuf[8]);
        } else if (sysexBuf[7] == 0x15) {
          bool isDrum = (sysexBuf[8] != 0);
          AudioEngine::setChannelDrumMode(targetCh, isDrum);
          if (isDrum) {
            AudioEngine::setBank(targetCh, 128);
            AudioEngine::programChange(targetCh, 0);
          }
        } else if (sysexBuf[7] == 0x16) {
          AudioEngine::setChannelKeyShift(targetCh, (int8_t)sysexBuf[8] - 64);
        } else if (sysexBuf[7] == 0x18) {
          AudioEngine::setChannelTuningOffset(
              targetCh, ((float)sysexBuf[8] - 64.0f) / 64.0f);
        } else if (sysexBuf[7] == 0x20) {
          AudioEngine::controlChange(targetCh, 74, sysexBuf[8]);
        } else if (sysexBuf[7] == 0x21) {
          AudioEngine::controlChange(targetCh, 71, sysexBuf[8]);
        } else if (sysexBuf[7] == 0x22) {
          AudioEngine::controlChange(targetCh, 73, sysexBuf[8]);
        } else if (sysexBuf[7] == 0x23) {
          AudioEngine::controlChange(targetCh, 75, sysexBuf[8]);
        } else if (sysexBuf[7] == 0x24) {
          AudioEngine::controlChange(targetCh, 72, sysexBuf[8]);
        } else if (sysexBuf[7] == 0x33) {
          AudioEngine::controlChange(targetCh, 91, sysexBuf[8]);
        } else if (sysexBuf[7] == 0x34) {
          AudioEngine::controlChange(targetCh, 93, sysexBuf[8]);
        }
      } else if (sysexLen >= 10 && sysexBuf[1] == 0x41 && sysexBuf[3] == 0x42 &&
                 sysexBuf[4] == 0x12 && sysexBuf[5] == 0x41) {
        // Roland GS Drum Setup
        uint8_t param = sysexBuf[6] & 0x0F;
        uint8_t key = sysexBuf[7];
        uint8_t val = sysexBuf[8];
        if (param == 0x01) {
          AudioEngine::setDrumKeyPitchDirect(key, (int8_t)val - 64);
        } else if (param == 0x04) {
          AudioEngine::setDrumKeyLevelDirect(key, val);
        } else if (param == 0x05) {
          AudioEngine::setDrumKeyPanDirect(key, val);
        }
      } else if (sysexLen >= 19 &&
                 (sysexBuf[1] == 0x7E || sysexBuf[1] == 0x7F) &&
                 sysexBuf[3] == 0x08 && sysexBuf[4] == 0x08) {
        // GM2 Scale/Octave Tuning
        int8_t scale[12];
        for (int i = 0; i < 12; i++) {
          scale[i] = (int8_t)sysexBuf[7 + i] - 64;
        }
        uint16_t chMask = ((uint16_t)sysexBuf[5] << 7) | sysexBuf[6];
        for (int ch = 0; ch < 16; ch++) {
          if (chMask & (1 << ch)) {
            AudioEngine::setScaleTuning(ch, scale);
          }
        }
      } else if (sysexLen >= 8 &&
                 (sysexBuf[1] == 0x7E || sysexBuf[1] == 0x7F) &&
                 sysexBuf[3] == 0x04 && sysexBuf[4] == 0x01) {
        // Master Volume
        uint16_t rawVol = (uint16_t)sysexBuf[5] | ((uint16_t)sysexBuf[6] << 7);
        uint8_t vol = (uint8_t)((rawVol * 100) / 16383);
        AudioEngine::setMasterVolume(vol);
      }
      sysexLen = 0;
      sysexOverflow = false;
      if (b == 0xF0) {
        inSysEx = true;
        sysexBuf[sysexLen++] = b;
      }
      return;
    } else if (b >= 0xF8) {
      return;
    } else {
      if (sysexLen < sizeof(sysexBuf) - 1) {
        sysexBuf[sysexLen++] = b;
      } else {
        sysexOverflow = true;
      }
      return;
    }
  }
}

void MIDIParser::clearAllVU() {
  for (int i = 0; i < 16; i++) {
    channels[i].vuLevel = 0;
  }
}

void MIDIParser::update() {
  uint32_t now = millis();

  // VU 미터 부드러운 감쇠 루틴 (35ms 주기)
  static unsigned long lastDecayTime = 0;
  if (now - lastDecayTime >= 35) {
    lastDecayTime = now;
    for (int i = 0; i < 16; i++) {
      if (channels[i].vuLevel > 0) {
        if (now - channels[i].lastNoteTime > 120) {
          if (channels[i].vuLevel > 2)
            channels[i].vuLevel -= 2;
          else
            channels[i].vuLevel = 0;
        }
      }
    }
  }
}
