#include "midi_parser.h"
#include "audio_engine.h"
#include "config.h"
#include "display_ui.h"
#include "la32_synth.h"
#include "led_indicator.h"
#include "midi_sequencer.h"

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
static unsigned long lastMidiEventTime = 0;
static SynthMode g_synth_mode = SYNTH_MODE_GM;

static HardwareSerial SerialMIDI(2);

SynthMode MIDIParser::getSynthMode() { return g_synth_mode; }

void MIDIParser::setSynthMode(SynthMode mode) {
  if (g_synth_mode != mode) {
    g_synth_mode = mode;
    AudioEngine::setMasterVolumeDirect(AudioEngine::getMasterVolume());
  }
}

const char *MIDIParser::getSynthModeString() {
  switch (g_synth_mode) {
  case SYNTH_MODE_GM2:
    return "[GM2]";
  case SYNTH_MODE_GS:
    return "[GS]";
  case SYNTH_MODE_MT32:
    return "[MT-32]";
  case SYNTH_MODE_GM:
  default:
    return "[GM]";
  }
}

static void serialMidiTask(void* pv) {
    while (true) {
        // 데이터 유무와 상관없이 update()를 실행하여 시리얼 수신, 2초 무신호 GM 복구, VU 감쇠를 동시 처리
        MIDIParser::update();
        vTaskDelay(pdMS_TO_TICKS(1));
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
  }
  channels[9].program = 0; // Drum Kit
  lastMidiEventTime = 0;

  // Core 0에서 화면/네트워크(1~2)보다 높은 우선순위 4로 생성
  xTaskCreatePinnedToCore(serialMidiTask, "SerialMIDI", 4096, NULL, 4, NULL, 0);
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

void MIDIParser::processCompleteMessage() {
  lastMidiEventTime = millis();
  DisplayUI::onExternalMIDIActivity();
  uint8_t status = msgBuffer[0] & 0xF0;
  uint8_t channel = msgBuffer[0] & 0x0F;

  switch (status) {
  case 0x90: { // Note On
    uint8_t note = msgBuffer[1];
    uint8_t vel = msgBuffer[2];
    if (vel > 0) {
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
    AudioEngine::programChange(channel, prog);
    lastActiveChannel = channel;
    break;
  }
  case 0xB0: { // Control Change
    uint8_t ctrl = msgBuffer[1];
    uint8_t val = msgBuffer[2];
    
    // Bank Select MSB (CC 0)
    if (ctrl == 0) {
      if (channel == 9) {
        // 드럼 채널 CC 0 = 127 (GS CM-64/32 Kit)
        if (val == 127 && g_synth_mode != SYNTH_MODE_MT32) {
          MIDIParser::setSynthMode(SYNTH_MODE_GS);
        }
      } else {
        if (val == 127) {
          // Bank 127 수신 시 MT-32 모드
          MIDIParser::setSynthMode(SYNTH_MODE_MT32);
          AudioEngine::applyMT32ModeDirect();
        } else if (val > 0) {
          // Bank 1~126 수신 시 GS 모드 및 해당 뱅크 지정
          MIDIParser::setSynthMode(SYNTH_MODE_GS);
          AudioEngine::setBank(channel, val);
        } else {
          // [수정] val == 0: MT-32/GS 모드를 해제하지 않고 해당 채널 뱅크만 0으로 변경
          AudioEngine::setBank(channel, 0);
        }
      }
    }

    if (ctrl == 7) channels[channel].volume = val;
    if (ctrl == 11) channels[channel].expression = val;

    // RPN 등록
    if (ctrl == 101) channelRPN[channel] = (channelRPN[channel] & 0x007F) | ((uint16_t)val << 7);
    if (ctrl == 100) channelRPN[channel] = (channelRPN[channel] & 0x3F80) | (val & 0x7F);

    if (ctrl == 99 || ctrl == 98) {
      channelRPN[channel] = 0x7F7F; // NRPN 수신 시 RPN 무효화
    }

    if (ctrl == 6) { // Data Entry MSB
      if (channelRPN[channel] == 0x0000) { // Pitch Bend Sensitivity (RPN 00 00)
        uint8_t range = (val > 24) ? 24 : val;
        AudioEngine::setPitchRange(channel, (float)range);
        channelRPN[channel] = 0x7F7F;
      }
    }
    if (ctrl == 121) {
      channelRPN[channel] = 0x7F7F;
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

  // 1. SysEx 수신 중이거나 0xF0가 들어온 경우
  if (inSysEx || b == 0xF0) {
    parseSysExByte(b);
    // SysEx를 끝내면서 들어온 일반 상태 바이트(0x80~0xEF)는 아래로 내려보내 정상 처리하고,
    // SysEx가 계속 진행 중이거나 F0/F7 바이트 자체인 경우에만 즉시 리턴
    if (inSysEx || b == 0xF0 || b == 0xF7)
      return;
  }

  // 2. F0 없이 들어오는 Roland Raw SysEx Escape (0x41 0x10 0x16 ...) 감지
  if (msgIndex == 0 && runningStatus == 0 && b == 0x41) {
    inSysEx = true;
    sysexOverflow = false;
    sysexLen = 0;
    sysexBuf[sysexLen++] = b;
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
      if (sysexOverflow) {
        sysexLen = 0;
        sysexOverflow = false;
        return; // 버퍼 초과로 잘린 손상 데이터는 파싱 건너뜀
      }
      if (b == 0xF7) {
        sysexBuf[sysexLen++] = b;
      }

      // [핵심] 시리얼로 실제 도착한 SysEx 바이트를 시리얼 모니터로 즉시 출력
      Serial.printf("[RX SysEx %d B] ", sysexLen);
      for (int i = 0; i < sysexLen && i < 12; i++) Serial.printf("%02X ", sysexBuf[i]);
      Serial.println();

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
        MIDIParser::setSynthMode(SYNTH_MODE_MT32);
        AudioEngine::setMT32ReverbDirect(
            sysexBuf[8], sysexBuf[9], (sysexLen >= 13 ? sysexBuf[10] : 64));
      } else if (sysexLen >= 14 && sysexBuf[1] == 0x41 && sysexBuf[3] == 0x16 &&
                 sysexBuf[4] == 0x12 &&
                 (sysexBuf[5] == 0x04 || sysexBuf[5] == 0x05 ||
                  sysexBuf[5] == 0x08)) {
        // Roland MT-32 Timbre Temp / Patch Temp Memory Dump (체크섬 없이 즉시 모드 적용)
        MIDIParser::setSynthMode(SYNTH_MODE_MT32);
        AudioEngine::applyMT32ModeDirect();

        uint8_t targetPart = sysexBuf[6] & 0x07; // Part 1~8 -> Channel 1~8
        uint8_t targetCh = targetPart + 1;       // MT-32 Part 1 = MIDI Ch 2(index 1)
        SemaphoreHandle_t mutex = AudioEngine::getMutex();
        if (mutex)
          xSemaphoreTake(mutex, pdMS_TO_TICKS(50));
        LA32SynthEngine::setCustomTimbre(targetCh, &sysexBuf[8],
                                         sysexLen - 9);
        if (mutex)
          xSemaphoreGive(mutex);
      } else if (sysexLen >= 14 && sysexBuf[1] == 0x41 && sysexBuf[3] == 0x16 &&
                 sysexBuf[4] == 0x12 && sysexBuf[5] == 0x05 &&
                 (sysexBuf[7] == 0x00 || sysexBuf[7] == 0x02)) {
        // Roland MT-32 Patch Temp Memory (Key Shift, Fine Tune, Bender Range)
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
        uint8_t key = sysexBuf[8];
        uint8_t level = (sysexLen >= 13) ? sysexBuf[10] : 100;
        uint8_t pan = (sysexLen >= 14) ? sysexBuf[11] : 7;
        AudioEngine::setDrumKeyLevelDirect(key, level);
        AudioEngine::setDrumKeyPanDirect(key, pan);
      } else if (sysexLen >= 6 && sysexBuf[1] == 0x7E && sysexBuf[3] == 0x09 &&
                 (sysexBuf[4] == 0x01 || sysexBuf[4] == 0x03)) {
        // GM1 / GM2 System On
        MIDIParser::setSynthMode((sysexBuf[4] == 0x03) ? SYNTH_MODE_GM2 : SYNTH_MODE_GM);
        AudioEngine::applyGMModeDirect();
        AudioEngine::systemReset();
        clearAllVU();
      } else if (sysexLen >= 5 && sysexBuf[1] == 0x41 && sysexBuf[3] == 0x16) {
        // Roland MT-32 SysEx (F0 41 <dev> 16 ...)
        if (g_synth_mode != SYNTH_MODE_MT32) {
          MIDIParser::setSynthMode(SYNTH_MODE_MT32);
          AudioEngine::applyMT32ModeDirect();
          clearAllVU();
        }
      } else if (sysexLen >= 11 && sysexBuf[1] == 0x41 && sysexBuf[3] == 0x42 &&
                 sysexBuf[4] == 0x12 && sysexBuf[5] == 0x40 &&
                 sysexBuf[6] == 0x00 && sysexBuf[7] == 0x7F) {
        // Roland GS Reset
        MIDIParser::setSynthMode(SYNTH_MODE_GS);
        AudioEngine::applyGSModeDirect();
        AudioEngine::systemReset();
        clearAllVU();
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
  // 1. UART 수신 버퍼 처리
  int avail = SerialMIDI.available();
  if (avail > 0) {
    if (avail > 256)
      avail = 256;
    uint8_t rxBuf[256];
    int count = SerialMIDI.readBytes(rxBuf, avail);
    for (int i = 0; i < count; i++) {
      parseByte(rxBuf[i]);
    }
  }

  // 시리얼 무신호 2초 감지 시 GM 모드로 자동 복구
  if (lastMidiEventTime > 0 && (millis() - lastMidiEventTime >= 2000)) {
    if (g_synth_mode != SYNTH_MODE_GM) {
      MIDIParser::setSynthMode(SYNTH_MODE_GM);
      AudioEngine::applyGMModeDirect();
      clearAllVU();
    }
    lastMidiEventTime = 0;
  }

  // 3. VU 미터 부드러운 감쇠 루틴 (35ms 주기)
  static unsigned long lastDecayTime = 0;
  if (millis() - lastDecayTime >= 35) {
    lastDecayTime = millis();
    for (int i = 0; i < 16; i++) {
      if (channels[i].vuLevel > 0) {
        if (millis() - channels[i].lastNoteTime > 120) {
          if (channels[i].vuLevel > 2)
            channels[i].vuLevel -= 2;
          else
            channels[i].vuLevel = 0;
        }
      }
    }
  }
}
