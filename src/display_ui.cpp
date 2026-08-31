#include "display_ui.h"
#include "audio_engine.h"
#include "la32_synth.h"
#include "config.h"
#include "game_engine.h"
#include "led_indicator.h"
#include "midi_parser.h"
#include "midi_sequencer.h"
#include "oled_logo_data.h"
#include "time_manager.h"
#include "u8g2_font_galmuri9.h"
#include "wifi_manager.h"
#include <LittleFS.h>
#include <Preferences.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <vector>

// I2C SSD1306 U8g2 인스턴스 (HW I2C) — 핀은 Wire.begin()에서 설정
static U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0,
                                                /* reset=*/U8X8_PIN_NONE);
static bool oledAvailable = false;

ScreenMode DisplayUI::currentMode = SCREEN_MAIN_MIDI;
int DisplayUI::menuIndex = 0;
int DisplayUI::midiMenuIndex = 0;
int DisplayUI::synthModeMenuIndex = 0;
int DisplayUI::manualModeMenuIndex = 0;
int DisplayUI::sfMenuIndex = 0;
int DisplayUI::baudMenuIndex = 0;
int DisplayUI::audioTestMenuIndex = 0;
int DisplayUI::audioOutputMenuIndex = 0;
int DisplayUI::ledStatusMenuIndex = 0;
int DisplayUI::languageMenuIndex = 0;
int DisplayUI::gamesMenuIndex = 0;
int DisplayUI::aboutScrollOffset = 0;
bool DisplayUI::isKorean = true;
unsigned long DisplayUI::lastInteractionTime = 0;
unsigned long DisplayUI::lastActivityTime = 0;
bool DisplayUI::isSleeping = false;
char DisplayUI::toastMsg[48] = "";
unsigned long DisplayUI::toastEndTime = 0;
bool DisplayUI::fontManagementEnabled = false;

bool DisplayUI::isKoreanMode() { return isKorean; }

ScreenMode DisplayUI::getMode() { return currentMode; }

void DisplayUI::setFontManagementEnabled(bool enabled) {
  fontManagementEnabled = enabled;
}

bool DisplayUI::isFontManagementEnabled() { return fontManagementEnabled; }

void DisplayUI::onExternalMIDIActivity() {
  wakeup();
  lastActivityTime = millis();
  if (MIDISequencer::getState() == SEQ_PLAYING) {
    MIDISequencer::stop();
  }
  if (currentMode == SCREEN_GAME_RUNNING || currentMode == SCREEN_MENU_GAMES) {
    GameEngine::exitGame();
    AudioEngine::systemReset();
    currentMode = SCREEN_MAIN_MIDI;
  }
}

void DisplayUI::setKoreanMode(bool ko) {
  isKorean = ko;
  Preferences prefs;
  prefs.begin("settings", false);
  prefs.putString("lang", ko ? "ko" : "en");
  prefs.end();
}

static std::vector<String> fontFileList;
static std::vector<String> midiFileList;
static bool s_fontListDirty = true;
static bool s_midiListDirty = true;

void DisplayUI::invalidateFileListCache() {
  s_fontListDirty = true;
  s_midiListDirty = true;
}

static void updateFontFileList() {
  if (!s_fontListDirty)
    return; // 캐시 유효 시 0ms 즉시 리턴
  s_fontListDirty = false;
  fontFileList.clear();
  fontFileList.reserve(32);
  File root = LittleFS.open("/");
  if (!root || !root.isDirectory())
    return;

  File file = root.openNextFile();
  while (file) {
    String name = file.name();
    if (name.endsWith(".sf2") || name.endsWith(".SF2")) {
      if (!name.startsWith("/"))
        name = "/" + name;
      fontFileList.push_back(name);
    }
    file = root.openNextFile();
  }
}

void DisplayUI::updateMidiFileList() {
  if (!s_midiListDirty)
    return; // 캐시 유효 시 0ms 즉시 리턴
  s_midiListDirty = false;
  midiFileList.clear();
  midiFileList.reserve(64);
  File root = LittleFS.open("/");
  if (!root || !root.isDirectory())
    return;

  File file = root.openNextFile();
  while (file) {
    String name = file.name();
    if (name.endsWith(".mid") || name.endsWith(".MID")) {
      if (name.startsWith("/"))
        name = name.substring(1);
      midiFileList.push_back(name);
    }
    file = root.openNextFile();
  }
}

static TaskHandle_t s_displayTaskHandle = nullptr;

static void displayTask(void* pvParameters) {
    while (true) {
        DisplayUI::update();
        vTaskDelay(pdMS_TO_TICKS(10)); // 10ms 양보 (Core 0의 UART 및 BGM 시퀀서 태스크 100% 보장)
    }
}

void DisplayUI::begin() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(800000); // 800kHz Fast-mode Plus I2C (전송 대기시간 50% 단축)
  Wire.setTimeOut(50);   // I2C 타임아웃 50ms (행 방지)

  // I2C 버스에서 OLED(0x3C) 존재 여부 확인
  Wire.beginTransmission(OLED_I2C_ADDR);
  uint8_t err = Wire.endTransmission();
  if (err != 0) {
    oledAvailable = false;
    lastInteractionTime = millis();
    return;
  }

  oledAvailable = true;
  u8g2.begin();
  u8g2.enableUTF8Print(); // 한글 UTF-8 렌더링 엔진 활성화
  u8g2.setBusClock(800000);
  u8g2.setFont(u8g2_font_galmuri9);
  lastInteractionTime = millis();
  lastActivityTime = millis();
  isSleeping = false;

  Preferences prefs;
  prefs.begin("settings", true);
  String lang = prefs.getString("lang", "ko");
  isKorean = (lang != "en");
  prefs.end();

  // 1. 부팅 로고 스플래시 화면 (oled_logo.png 단독 정중앙 1.5초 표시)
  u8g2.clearBuffer();
  int logoX = (128 - OLED_LOGO_WIDTH) / 2;
  int logoY = (64 - OLED_LOGO_HEIGHT) / 2;
  u8g2.drawXBMP(logoX, logoY, OLED_LOGO_WIDTH, OLED_LOGO_HEIGHT,
                OLED_LOGO_BITS);
  u8g2.sendBuffer();
  delay(1500); // 로고 1.5초 유지

  // 2. 웰컴 16채널 파도타기 애니메이션
  float waveBars[16];
  for (int t = 0; t < 22; t++) {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(0, 7, "WaveCanvas Nano RS");
    u8g2.drawStr(128 - 30, 7, "READY");
    u8g2.drawHLine(0, 9, 128);

    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.drawStr(0, 23, "System Ready");
    u8g2.setFont(u8g2_font_4x6_tf);
    u8g2.drawStr(0, 36, "1-16ch MIDI Level");

    for (int i = 0; i < 16; i++) {
      float angle = (float)t * 0.28f - (float)i * 0.38f;
      float val = 8.0f + 8.0f * sinf(angle);
      if (val < 0.0f)
        val = 0.0f;
      if (val > 16.0f)
        val = 16.0f;
      waveBars[i] = val;

      int barHeight = (int)roundf(val);
      int x = i * 8;
      int y = 63 - barHeight;
      if (barHeight > 0) {
        u8g2.drawBox(x, y, 6, barHeight);
      } else {
        u8g2.drawHLine(x, 63, 6);
      }
    }
    u8g2.sendBuffer();
    delay(25);
  }

  // 3. 파도타기 모양 그대로 부드럽게 스르륵 올라가서 꽉 채우기 (Smooth Rise to
  // Fill)
  bool allFilled = false;
  while (!allFilled) {
    allFilled = true;
    for (int i = 0; i < 16; i++) {
      if (waveBars[i] < 16.0f) {
        waveBars[i] += 1.5f;
        if (waveBars[i] > 16.0f)
          waveBars[i] = 16.0f;
        else
          allFilled = false;
      }
    }

    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(0, 7, "WaveCanvas Nano RS");
    u8g2.drawStr(128 - 30, 7, "READY");
    u8g2.drawHLine(0, 9, 128);
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.drawStr(0, 23, "System Ready");
    u8g2.setFont(u8g2_font_4x6_tf);
    u8g2.drawStr(0, 36, "1-16ch MIDI Level");

    for (int i = 0; i < 16; i++) {
      int barHeight = (int)roundf(waveBars[i]);
      int x = i * 8;
      int y = 63 - barHeight;
      if (barHeight > 0) {
        u8g2.drawBox(x, y, 6, barHeight);
      } else {
        u8g2.drawHLine(x, 63, 6);
      }
    }
    u8g2.sendBuffer();
    delay(25);
  }

  // 4. 꽉 찬 상태 유지 (100ms)
  delay(100);

  // 5. 스르륵 0으로 감쇠 (Smooth Decay to 0)
  for (int h = 16; h >= 0; h -= 2) {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(0, 7, "WaveCanvas Nano RS");
    u8g2.drawStr(128 - 30, 7, "READY");
    u8g2.drawHLine(0, 9, 128);
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.drawStr(0, 23, "System Ready");
    u8g2.setFont(u8g2_font_4x6_tf);
    u8g2.drawStr(0, 36, "1-16ch MIDI Level");

    for (int i = 0; i < 16; i++) {
      int x = i * 8;
      int y = 63 - h;
      if (h > 0) {
        u8g2.drawBox(x, y, 6, h);
      } else {
        u8g2.drawHLine(x, 63, 6);
      }
    }
    u8g2.sendBuffer();
    delay(25);
  }

  // Core 0에 낮은 우선순위(Priority 1, 스택 4096B)로 백그라운드 디스플레이 렌더링 태스크 생성
  xTaskCreatePinnedToCore(
      displayTask,
      "displayTask",
      4096,
      NULL,
      1,
      &s_displayTaskHandle,
      0
  );
  DEBUG_REG_DISPLAY_TASK(s_displayTaskHandle);
}

void DisplayUI::wakeup() {
  lastActivityTime = millis();
  if (isSleeping && oledAvailable) {
    u8g2.setPowerSave(0);
    isSleeping = false;
  }
}

static uint8_t s_displayCh = 0;
static unsigned long s_lastChCycleTime = 0;

void DisplayUI::resetChannelDisplay() {
  s_displayCh = 0;
  s_lastChCycleTime = 0;
}

void DisplayUI::showToast(const char *message, uint16_t durationMs) {
  wakeup();
  strncpy(toastMsg, message, sizeof(toastMsg) - 1);
  toastEndTime = millis() + durationMs;
}

void DisplayUI::drawMainMIDIScreen() {
  // 1. 헤더 (실시간 시계 & 신디사이저 모드 뱃지 [GM]/[GS]/[MT-32] & 볼륨)
  u8g2.setFont(u8g2_font_5x7_tf);
  String timeStr = TimeManager::getFormattedTime();
  u8g2.drawStr(0, 7, timeStr.c_str());

  char hdrRight[32];
  snprintf(hdrRight, sizeof(hdrRight), "%s V:%d%%",
           MIDIParser::getIndicatorString(), AudioEngine::getMasterVolume());
  int hdrWidth = u8g2.getStrWidth(hdrRight);
  u8g2.drawStr(128 - hdrWidth, 7, hdrRight);

  u8g2.drawHLine(0, 9, 128);

  // 2. 현재 연주 중인 채널 & GM 악기 이름 (5초 주기 활성 채널 자동 순환)
  uint8_t lastActive = MIDIParser::getLastActiveChannel();

  // 5초마다 현재 소리가 울리고 있는 활성 채널들 중 다음 채널로 여유 있게 순환
  if (millis() - s_lastChCycleTime > 5000) {
    s_lastChCycleTime = millis();
    int nextCh = -1;
    for (int step = 1; step <= 16; step++) {
      int c = (s_displayCh + step) % 16;
      const ChannelStatus &cs = MIDIParser::getChannelStatus(c);
      if (cs.vuLevel > 0 ||
          (cs.lastNoteTime > 0 && (millis() - cs.lastNoteTime < 6000))) {
        nextCh = c;
        break;
      }
    }
    if (nextCh >= 0) {
      s_displayCh = (uint8_t)nextCh;
    } else {
      s_displayCh = lastActive;
    }
  }

  const ChannelStatus &chStat = MIDIParser::getChannelStatus(s_displayCh);
  const char *instName = LA32SynthEngine::getCustomTimbreName(s_displayCh);
  if (!instName || instName[0] == '\0') {
    instName = MIDIParser::getInstrumentName(chStat.program, (s_displayCh == 9));
  }

  u8g2.setFont(u8g2_font_galmuri9);
  char chStr[10];
  snprintf(chStr, sizeof(chStr), "Ch%02d", s_displayCh + 1);
  u8g2.drawUTF8(0, 26, chStr);
  u8g2.drawUTF8(34, 26, instName);

  // 3. 16채널 실시간 VU 미터 (Roland Sound Canvas 감성)
  // 화면 하단 y: 46 ~ 62 (높이 16px), 채널당 7px 너비 + 1px 간격 = 128px
  u8g2.setFont(u8g2_font_4x6_tf);
  u8g2.drawStr(0, 38, "1-16ch MIDI Level");

  for (int i = 0; i < 16; i++) {
    const ChannelStatus &cs = MIDIParser::getChannelStatus(i);
    int barHeight = cs.vuLevel; // 0 ~ 15
    if (barHeight > 16)
      barHeight = 16;

    int x = i * 8;
    int y = 63 - barHeight;

    if (barHeight > 0) {
      u8g2.drawBox(x, y, 6, barHeight);
    } else {
      u8g2.drawHLine(x, 63, 6); // 무음 시 1픽셀 베이스라인
    }
  }
}

static void drawMenuItemWithMarquee(int x, int y, int maxWidth,
                                    const String &text, bool isSelected) {
  int textW = u8g2.getUTF8Width(text.c_str());

  if (isSelected) {
    u8g2.drawBox(0, y - 9, 123, 10);
    u8g2.setDrawColor(0);

    if (textW > maxWidth) {
      unsigned long now = millis();
      int overflow = textW - maxWidth + 8;
      unsigned long cycle = 1500 + (unsigned long)overflow * 40 + 1200;
      unsigned long phase = now % cycle;
      int offset = 0;
      if (phase < 1500) {
        offset = 0;
      } else if (phase < 1500 + (unsigned long)overflow * 40) {
        offset = (int)((phase - 1500) / 40);
      } else {
        offset = overflow;
      }

      u8g2.setClipWindow(0, y - 9, 123, y + 1);
      u8g2.drawUTF8(x - offset, y, text.c_str());
      u8g2.setMaxClipWindow();
    } else {
      u8g2.drawUTF8(x, y, text.c_str());
    }
    u8g2.setDrawColor(1);
  } else {
    if (textW > maxWidth) {
      u8g2.setClipWindow(0, y - 9, 123, y + 1);
      u8g2.drawUTF8(x, y, text.c_str());
      u8g2.setMaxClipWindow();
    } else {
      u8g2.drawUTF8(x, y, text.c_str());
    }
  }
}

void DisplayUI::drawScrollbar(int x, int y, int w, int h, int totalItems,
                              int currentIndex, int visibleItems) {
  if (totalItems <= visibleItems || totalItems <= 0)
    return;
  u8g2.drawVLine(x + w / 2, y, h);
  int thumbH = max(4, (h * visibleItems) / totalItems);
  int maxIndex = (totalItems > 1) ? (totalItems - 1) : 1;
  int curIndex = min(currentIndex, maxIndex);
  if (curIndex < 0)
    curIndex = 0;
  int thumbY = y + (curIndex * (h - thumbH)) / maxIndex;
  u8g2.drawBox(x, thumbY, w, thumbH);
}

void DisplayUI::drawMainMenu() {
  u8g2.setFont(u8g2_font_galmuri9);
  u8g2.drawUTF8(0, 9, isKorean ? "[ 설정 메뉴 ]" : "[ SETTINGS MENU ]");
  u8g2.drawHLine(0, 11, 128);

  const char *items_ko_full[] = {
      "1. MIDI 보관함",        "2. 신스 모드 설정",   "3. 사운드폰트 선택",
      "4. Wi-Fi 정보 & IP",    "5. MIDI 전송 속도",   "6. 오디오 테스트",
      "7. 소리 출력 설정",     "8. LED 상태표시등",   "9. 언어 설정 (Lang)",
      "10. 긴급 리셋 (Panic)", "11. 기기 정보 (About)", "12. < 메인 화면으로 >"};

  const char *items_en_full[] = {
      "1. MIDI Library",       "2. Synth Mode",       "3. SoundFont Select",
      "4. Wi-Fi Info & IP",    "5. MIDI Baud Rate",   "6. Audio Test",
      "7. Audio Output",       "8. LED Status",       "9. Language",
      "10. MIDI Panic",        "11. About",           "12. < Back to Main >"};

  const char *items_ko_hide[] = {
      "1. MIDI 보관함",        "2. 신스 모드 설정",   "3. Wi-Fi 정보 & IP",
      "4. MIDI 전송 속도",     "5. 오디오 테스트",     "6. 소리 출력 설정",
      "7. LED 상태표시등",     "8. 언어 설정 (Lang)",  "9. 긴급 리셋 (Panic)",
      "10. 기기 정보 (About)", "11. < 메인 화면으로 >"};

  const char *items_en_hide[] = {
      "1. MIDI Library",       "2. Synth Mode",       "3. Wi-Fi Info & IP",
      "4. MIDI Baud Rate",     "5. Audio Test",       "6. Audio Output",
      "7. LED Status",         "8. Language",         "9. MIDI Panic",
      "10. About",             "11. < Back to Main >"};

  const char **items;
  int total;
  if (fontManagementEnabled) {
    items = isKorean ? items_ko_full : items_en_full;
    total = 12;
  } else {
    items = isKorean ? items_ko_hide : items_en_hide;
    total = 11;
  }

  int visible = 4;
  int start = (menuIndex > 2) ? (menuIndex - 2) : 0;
  if (start > total - visible)
    start = total - visible;
  if (start < 0)
    start = 0;
  int end = min(start + visible, total);

  for (int i = start; i < end; i++) {
    int y = 26 + ((i - start) * 11);
    drawMenuItemWithMarquee(2, y, 118, items[i], (i == menuIndex));
  }
  drawScrollbar(125, 16, 2, 47, total, menuIndex, visible);
}

void DisplayUI::drawMIDILibraryMenu() {
  u8g2.setFont(u8g2_font_galmuri9);
  u8g2.drawUTF8(0, 9, isKorean ? "[ MIDI 보관함 ]" : "[ MIDI LIBRARY ]");
  u8g2.drawHLine(0, 11, 128);

  int total = midiFileList.size() + 1; // 0번은 < Back >
  int visible = 4;
  int start = (midiMenuIndex > 2) ? (midiMenuIndex - 2) : 0;
  if (start > total - visible)
    start = total - visible;
  if (start < 0)
    start = 0;
  int end = min(start + visible, total);

  String currentSong = MIDISequencer::getCurrentSongName();
  if (currentSong.startsWith("/"))
    currentSong = currentSong.substring(1);
  bool isSeqPlaying = (MIDISequencer::getState() == SEQ_PLAYING);

  for (int i = start; i < end; i++) {
    int y = 26 + ((i - start) * 11);
    String displayText;
    if (i == 0) {
      displayText = isKorean ? "< 메뉴로 돌아가기 >" : "< Back to Menu >";
    } else {
      String name = midiFileList[i - 1];
      bool isCurrent = (isSeqPlaying && name == currentSong);
      displayText = (isCurrent ? "* " : "  ") + name;
    }

    drawMenuItemWithMarquee(2, y, 118, displayText, (i == midiMenuIndex));
  }
  drawScrollbar(125, 16, 2, 47, total, midiMenuIndex, visible);
}

void DisplayUI::drawSynthModeMenu() {
  u8g2.setFont(u8g2_font_galmuri9);
  u8g2.drawUTF8(0, 9, isKorean ? "[ 신스 모드 설정 ]" : "[ SYNTH MODE ]");
  u8g2.drawHLine(0, 11, 128);

  bool isAuto = (MIDIParser::getSynthPolicy() == SYNTH_POLICY_AUTO);
  const char *sItems_ko[] = {"스마트 모드 (Auto)", "수동 모드 (Manual)",
                             "< 메뉴로 돌아가기 >"};
  const char *sItems_en[] = {"Smart Mode (Auto)", "Manual Mode",
                             "< Back to Menu >"};
  const char **sItems = isKorean ? sItems_ko : sItems_en;

  for (int i = 0; i < 3; i++) {
    int y = 26 + (i * 11);
    bool isCurrent = (i == 0 && isAuto) || (i == 1 && !isAuto);
    char itemText[32];
    if (i == 2) {
      snprintf(itemText, sizeof(itemText), "%s", sItems[i]);
    } else {
      snprintf(itemText, sizeof(itemText), "%s%s", isCurrent ? "* " : "  ",
               sItems[i]);
    }

    if (i == synthModeMenuIndex) {
      u8g2.drawBox(0, y - 9, 128, 10);
      u8g2.setDrawColor(0);
      u8g2.drawUTF8(2, y, itemText);
      u8g2.setDrawColor(1);
    } else {
      u8g2.drawUTF8(2, y, itemText);
    }
  }
}

void DisplayUI::drawManualModeMenu() {
  u8g2.setFont(u8g2_font_galmuri9);
  u8g2.drawUTF8(0, 9, isKorean ? "[ 수동 모드 선택 ]" : "[ MANUAL MODE ]");
  u8g2.drawHLine(0, 11, 128);

  ManualSubMode curSub = MIDIParser::getManualSubMode();
  const char *mItems_ko[] = {"GM 모드", "GS 모드", "MT-32 모드",
                             "< 뒤로 가기 >"};
  const char *mItems_en[] = {"GM Mode", "GS Mode", "MT-32 Mode",
                             "< Back >"};
  const char **mItems = isKorean ? mItems_ko : mItems_en;

  for (int i = 0; i < 4; i++) {
    int y = 26 + (i * 11);
    bool isCurrent = (i == 0 && curSub == MANUAL_MODE_GM) ||
                     (i == 1 && curSub == MANUAL_MODE_GS) ||
                     (i == 2 && curSub == MANUAL_MODE_MT32);
    char itemText[32];
    if (i == 3) {
      snprintf(itemText, sizeof(itemText), "%s", mItems[i]);
    } else {
      snprintf(itemText, sizeof(itemText), "%s%s", isCurrent ? "* " : "  ",
               mItems[i]);
    }

    if (i == manualModeMenuIndex) {
      u8g2.drawBox(0, y - 9, 128, 10);
      u8g2.setDrawColor(0);
      u8g2.drawUTF8(2, y, itemText);
      u8g2.setDrawColor(1);
    } else {
      u8g2.drawUTF8(2, y, itemText);
    }
  }
}

void DisplayUI::drawSoundFontMenu() {
  u8g2.setFont(u8g2_font_galmuri9);
  u8g2.drawUTF8(0, 9,
                isKorean ? "[ 사운드폰트 선택 ]" : "[ SELECT SOUNDFONT ]");
  u8g2.drawHLine(0, 11, 128);

  int total = fontFileList.size() + 1; // 0번은 < Back >
  int visible = 4;
  int start = (sfMenuIndex > 2) ? (sfMenuIndex - 2) : 0;
  if (start > total - visible)
    start = total - visible;
  if (start < 0)
    start = 0;
  int end = min(start + visible, total);

  String currentFont = AudioEngine::getCurrentFontName();
  if (currentFont.startsWith("/"))
    currentFont = currentFont.substring(1);

  for (int i = start; i < end; i++) {
    int y = 26 + ((i - start) * 11);
    String displayText;
    if (i == 0) {
      displayText = isKorean ? "< 메뉴로 돌아가기 >" : "< Back to Menu >";
    } else {
      String name = fontFileList[i - 1];
      if (name.startsWith("/"))
        name = name.substring(1);
      bool isSelected = (name == currentFont);
      displayText = (isSelected ? "* " : "  ") + name;
    }

    drawMenuItemWithMarquee(2, y, 118, displayText, (i == sfMenuIndex));
  }
  drawScrollbar(125, 16, 2, 47, total, sfMenuIndex, visible);
}

void DisplayUI::drawWiFiInfoMenu() {
  u8g2.setFont(u8g2_font_galmuri9);
  u8g2.drawUTF8(0, 9, isKorean ? "[ 와이파이 상태 ]" : "[ WI-FI STATUS ]");
  u8g2.drawHLine(0, 11, 128);

  char buf[32];

  snprintf(buf, sizeof(buf), isKorean ? "모드: %s" : "Mode: %s",
           WiFiManager::getModeString());
  u8g2.drawUTF8(2, 26, buf);

  snprintf(buf, sizeof(buf), "SSID: %s", WiFiManager::getSSID());
  u8g2.drawUTF8(2, 37, buf);

  snprintf(buf, sizeof(buf), "IP  : %s", WiFiManager::getIPAddress().c_str());
  u8g2.drawUTF8(2, 48, buf);

  u8g2.drawUTF8(2, 59,
                isKorean ? "웹  : 80번 포트 (대기)" : "Web : Port 80 (Ready)");
}

void DisplayUI::drawBaudRateMenu() {
  u8g2.setFont(u8g2_font_galmuri9);
  u8g2.drawUTF8(0, 9, isKorean ? "[ MIDI 전송 속도 ]" : "[ MIDI BAUD RATE ]");
  u8g2.drawHLine(0, 11, 128);

  const char *bItems_en[] = {"38400 bps (SoftMPU)", "31250 bps (Standard)",
                             "115200 bps (Fast)", "< Back to Menu >"};
  const char *bItems_ko[] = {"38400 bps (SoftMPU)", "31250 bps (표준 MIDI)",
                             "115200 bps (고속)", "< 메뉴로 돌아가기 >"};
  const char **bItems = isKorean ? bItems_ko : bItems_en;
  uint32_t currentBaud = MIDIParser::getBaudRate();

  for (int i = 0; i < 4; i++) {
    int y = 26 + (i * 11);
    bool isCurrent = (i == 0 && currentBaud == 38400) ||
                     (i == 1 && currentBaud == 31250) ||
                     (i == 2 && currentBaud == 115200);
    char itemText[32];
    if (i == 3) {
      snprintf(itemText, sizeof(itemText), "%s", bItems[i]);
    } else {
      snprintf(itemText, sizeof(itemText), "%s%s", isCurrent ? "* " : "  ",
               bItems[i]);
    }

    if (i == baudMenuIndex) {
      u8g2.drawBox(0, y - 9, 128, 10);
      u8g2.setDrawColor(0);
      u8g2.drawUTF8(2, y, itemText);
      u8g2.setDrawColor(1);
    } else {
      u8g2.drawUTF8(2, y, itemText);
    }
  }
}

void DisplayUI::drawAudioTestMenu() {
  u8g2.setFont(u8g2_font_galmuri9);
  u8g2.drawUTF8(0, 9, isKorean ? "[ 오디오 테스트 ]" : "[ AUDIO TEST ]");
  u8g2.drawHLine(0, 11, 128);

  const char *tItems_ko[] = {"1. 피아노 C화음", "2. 기타 아르페지오",
                             "3. 드럼 키트", "4. 스테레오 테스트",
                             "< 메뉴로 돌아가기 >"};
  const char *tItems_en[] = {"1. Piano C-Chord", "2. Guitar Arpeggio",
                             "3. Drum Kit", "4. Stereo Test",
                             "< Back to Menu >"};
  const char **tItems = isKorean ? tItems_ko : tItems_en;

  int total = 5;
  int visible = 4;
  int start = (audioTestMenuIndex > 2) ? (audioTestMenuIndex - 2) : 0;
  if (start > total - visible)
    start = total - visible;
  if (start < 0)
    start = 0;
  int end = min(start + visible, total);

  for (int i = start; i < end; i++) {
    int y = 26 + ((i - start) * 11);
    drawMenuItemWithMarquee(2, y, 118, tItems[i], (i == audioTestMenuIndex));
  }
  drawScrollbar(125, 16, 2, 47, total, audioTestMenuIndex, visible);
}

void DisplayUI::drawAudioOutputMenu() {
  u8g2.setFont(u8g2_font_galmuri9);
  u8g2.drawUTF8(0, 9, isKorean ? "[ 소리 출력 설정 ]" : "[ AUDIO OUTPUT ]");
  u8g2.drawHLine(0, 11, 128);

  bool isMono = AudioEngine::isMonoMode();
  const char *oItems_ko[] = {"스테레오 (Stereo)", "모노 (Mono)",
                             "< 메뉴로 돌아가기 >"};
  const char *oItems_en[] = {"Stereo", "Mono", "< Back to Menu >"};
  const char **oItems = isKorean ? oItems_ko : oItems_en;

  for (int i = 0; i < 3; i++) {
    int y = 26 + (i * 11);
    bool isCurrent = (i == 0 && !isMono) || (i == 1 && isMono);
    char itemText[32];
    if (i == 2) {
      snprintf(itemText, sizeof(itemText), "%s", oItems[i]);
    } else {
      snprintf(itemText, sizeof(itemText), "%s%s", isCurrent ? "* " : "  ",
               oItems[i]);
    }

    if (i == audioOutputMenuIndex) {
      u8g2.drawBox(0, y - 9, 128, 10);
      u8g2.setDrawColor(0);
      u8g2.drawUTF8(2, y, itemText);
      u8g2.setDrawColor(1);
    } else {
      u8g2.drawUTF8(2, y, itemText);
    }
  }
}

void DisplayUI::drawLEDStatusMenu() {
  u8g2.setFont(u8g2_font_galmuri9);
  u8g2.drawUTF8(0, 9, isKorean ? "[ LED 상태표시등 ]" : "[ LED STATUS ]");
  u8g2.drawHLine(0, 11, 128);

  bool isEn = LEDIndicator::isEnabled();
  const char *lItems_ko[] = {"켜기 (정상 표시)", "끄기 (LED 소등)",
                             "< 메뉴로 돌아가기 >"};
  const char *lItems_en[] = {"Enabled (Normal)", "Disabled (Off)",
                             "< Back to Menu >"};
  const char **lItems = isKorean ? lItems_ko : lItems_en;

  for (int i = 0; i < 3; i++) {
    int y = 26 + (i * 11);
    bool isCurrent = (i == 0 && isEn) || (i == 1 && !isEn);
    char itemText[32];
    if (i == 2) {
      snprintf(itemText, sizeof(itemText), "%s", lItems[i]);
    } else {
      snprintf(itemText, sizeof(itemText), "%s%s", isCurrent ? "* " : "  ",
               lItems[i]);
    }

    if (i == ledStatusMenuIndex) {
      u8g2.drawBox(0, y - 9, 128, 10);
      u8g2.setDrawColor(0);
      u8g2.drawUTF8(2, y, itemText);
      u8g2.setDrawColor(1);
    } else {
      u8g2.drawUTF8(2, y, itemText);
    }
  }
}

void DisplayUI::drawLanguageMenu() {
  u8g2.setFont(u8g2_font_galmuri9);
  u8g2.drawUTF8(0, 9, isKorean ? "[ 언어 설정 ]" : "[ LANGUAGE ]");
  u8g2.drawHLine(0, 11, 128);

  const char *langItems[] = {"한국어 (Korean)", "English (영어)",
                             "< Back to Menu >"};

  for (int i = 0; i < 3; i++) {
    int y = 26 + (i * 11);
    bool isCurrent = (i == 0 && isKorean) || (i == 1 && !isKorean);
    char itemText[32];
    if (i == 2) {
      snprintf(itemText, sizeof(itemText), "%s",
               isKorean ? "< 메뉴로 돌아가기 >" : "< Back to Menu >");
    } else {
      snprintf(itemText, sizeof(itemText), "%s%s", isCurrent ? "* " : "  ",
               langItems[i]);
    }

    if (i == languageMenuIndex) {
      u8g2.drawBox(0, y - 9, 128, 10);
      u8g2.setDrawColor(0);
      u8g2.drawUTF8(2, y, itemText);
      u8g2.setDrawColor(1);
    } else {
      u8g2.drawUTF8(2, y, itemText);
    }
  }
}

void DisplayUI::drawAboutMenu() {
  u8g2.setFont(u8g2_font_galmuri9);
  u8g2.drawUTF8(0, 9,
                isKorean ? "[ 기기 및 시스템 정보 ]" : "[ ABOUT & SYSTEM ]");
  u8g2.drawHLine(0, 11, 128);

  char tempBuf[32];
  snprintf(tempBuf, sizeof(tempBuf),
           isKorean ? "온도 : %.1f C" : "Temp : %.1f C", temperatureRead());

  char heapBuf[32];
  snprintf(heapBuf, sizeof(heapBuf),
           isKorean ? "힙   : %u KB 가용" : "Heap : %u KB Free",
           (unsigned int)(ESP.getFreeHeap() / 1024));

  char psramBuf[32];
  snprintf(psramBuf, sizeof(psramBuf),
           isKorean ? "PSRAM: %.2f MB 가용" : "PSRAM: %.2f MB Free",
           (float)ESP.getFreePsram() / (1024.0f * 1024.0f));

  char flashBuf[32];
  snprintf(flashBuf, sizeof(flashBuf),
           isKorean ? "플래시: %.1f/16 MB (FS)" : "Flash: %.1f/16 MB (FS)",
           (float)LittleFS.usedBytes() / (1024.0f * 1024.0f));

  const char *lines_ko[] = {"WaveCanvas Nano RS",
                            "FW: v1.03 (May 1998)",
                            "개발: Nexisson Tech",
                            "--- 오픈소스 라이선스 ---",
                            "TSF: B.Schelling",
                            "Gervill: K.Helgason",
                            "DSP: Freeverb 3D",
                            "GUI: U8g2 & AsyncWeb",
                            "--- 하드웨어 사양 ---",
                            "칩셋: ESP32-S3 (240M)",
                            tempBuf,
                            heapBuf,
                            psramBuf,
                            flashBuf,
                            "< 뒤로가기 (클릭) >",
                            "",
                            "",
                            "< 길게 눌러 이스터에그 >"};

  const char *lines_en[] = {"WaveCanvas Nano RS",
                            "FW: v1.03 (May 1998)",
                            "Dev: Nexisson Tech",
                            "--- Open Source ---",
                            "TSF: B.Schelling",
                            "Gervill: K.Helgason",
                            "DSP: Freeverb 3D",
                            "GUI: U8g2 & AsyncWeb",
                            "--- Hardware Info ---",
                            "Chip: ESP32-S3 (240M)",
                            tempBuf,
                            heapBuf,
                            psramBuf,
                            flashBuf,
                            "< Back to Menu (Click) >",
                            "",
                            "",
                            "< Long Press: Easter Egg >"};

  const char **lines = isKorean ? lines_ko : lines_en;

  int totalLines = 18;
  int visibleLines = 4;
  if (aboutScrollOffset > totalLines - visibleLines)
    aboutScrollOffset = totalLines - visibleLines;
  if (aboutScrollOffset < 0)
    aboutScrollOffset = 0;

  for (int i = 0; i < visibleLines; i++) {
    int idx = aboutScrollOffset + i;
    if (idx >= totalLines)
      break;
    int y = 26 + (i * 11);
    if (strlen(lines[idx]) > 0) {
      u8g2.drawUTF8(2, y, lines[idx]);
    }
  }

  drawScrollbar(125, 16, 2, 47, totalLines, aboutScrollOffset, visibleLines);
}

void DisplayUI::drawGamesMenu() {
  u8g2.setFont(u8g2_font_galmuri9);
  u8g2.drawUTF8(0, 9, isKorean ? "[ 아케이드 게임 ]" : "[ ARCADE GAMES ]");
  u8g2.drawHLine(0, 11, 128);

  const char *gItems_ko[] = {"1. 가상 피아노",
                             "2. 핑퐁 (Pong)",
                             "3. 블록 쌓기 (Block Stack)",
                             "4. 벽돌깨기 (Brick Breaker)",
                             "5. 스네이크 (Snake)",
                             "< 메인 화면으로 돌아가기 >"};
  const char *gItems_en[] = {
      "1. Virtual Piano",        "2. Pong (1972)",   "3. Block Stack (8-Bit)",
      "4. Brick Breaker (1976)", "5. Snake (8-Bit)", "< Back to Main >"};
  const char **gItems = isKorean ? gItems_ko : gItems_en;

  int total = 6;
  int visible = 4;
  int start = (gamesMenuIndex > 2) ? (gamesMenuIndex - 2) : 0;
  if (start > total - visible)
    start = total - visible;
  if (start < 0)
    start = 0;
  int end = min(start + visible, total);

  for (int i = start; i < end; i++) {
    int y = 26 + ((i - start) * 11);
    drawMenuItemWithMarquee(2, y, 118, gItems[i], (i == gamesMenuIndex));
  }
  drawScrollbar(125, 16, 2, 47, total, gamesMenuIndex, visible);
}

void DisplayUI::drawToast() {
  if (millis() < toastEndTime && strlen(toastMsg) > 0) {
    u8g2.setFont(u8g2_font_galmuri9);
    int w = u8g2.getUTF8Width(toastMsg) + 12;
    int h = 18;
    int x = (128 - w) / 2;
    int y = 23;

    u8g2.setDrawColor(0);
    u8g2.drawBox(x, y, w, h);
    u8g2.setDrawColor(1);
    u8g2.drawFrame(x, y, w, h);
    u8g2.drawUTF8(x + 6, y + 13, toastMsg);
  }
}

void DisplayUI::handleEncoderEvent(EncoderEvent event) {
  if (event == ENC_NONE)
    return;

  wakeup();
  lastInteractionTime = millis();

  // 1. 5초 이상 초장기 롱프레스는 게임 실행 중일 때 강제 탈출
  if (event == ENC_BUTTON_VERY_LONG) {
    if (currentMode == SCREEN_GAME_RUNNING) {
      GameEngine::exitGame();
      currentMode = SCREEN_MENU_GAMES;
      showToast(isKorean ? "게임 종료" : "Game Exit", 1500);
      return;
    }
  }

  if (event == ENC_BUTTON_PANIC) {
    if (currentMode == SCREEN_GAME_RUNNING) {
      GameEngine::handleEncoderEvent(event);
      return;
    }
    AudioEngine::panic();
    showToast(isKorean ? "초기화 완료!" : "MIDI PANIC!");
    return;
  }

  // 2. 1.0초 롱프레스 처리
  if (event == ENC_BUTTON_LONG) {
    // About 화면에서 롱프레스 시 이스터에그 게임 메뉴 진입!
    if (currentMode == SCREEN_MENU_ABOUT) {
      bool isMidiPlaying = (MIDISequencer::getState() == SEQ_PLAYING &&
                            !MIDISequencer::isLoopEnabled()) ||
                           MIDIParser::isMIDIActive();
      if (isMidiPlaying) {
        showToast(isKorean ? "MIDI 연주 중 진입 불가" : "MIDI Playing: Locked",
                  1500);
        return;
      }
      gamesMenuIndex = 0;
      currentMode = SCREEN_MENU_GAMES;
      showToast(isKorean ? "🎮 아케이드 게임!" : "🎮 Arcade Games!", 1500);
      return;
    }
    // 게임 실행 중일 때는 게임 엔진 내부 롱프레스(하드드롭 등)로 위임
    if (currentMode == SCREEN_GAME_RUNNING) {
      GameEngine::handleEncoderEvent(event);
      return;
    }
    if (currentMode == SCREEN_MAIN_MIDI) {
      // 수동 모드일 때만 3개 모드 순환 변경
      if (MIDIParser::getSynthPolicy() == SYNTH_POLICY_MANUAL) {
        bool isPlaying = (MIDISequencer::getState() == SEQ_PLAYING) || MIDIParser::isMIDIActive();
        if (isPlaying) {
          showToast(isKorean ? "재생중 모드 변경 불가" : "Playing: Locked", 1500);
          return;
        }
        MIDIParser::cycleManualSubMode();
        const char *name =
            (MIDIParser::getManualSubMode() == MANUAL_MODE_GS)     ? "GS"
            : (MIDIParser::getManualSubMode() == MANUAL_MODE_MT32) ? "MT-32"
                                                                   : "GM";
        char buf[32];
        snprintf(buf, sizeof(buf), isKorean ? "수동: %s 모드" : "Manual: %s",
                 name);
        showToast(buf, 1200);
      }
      return;
    }
    return;
  }

  // 3. 게임 실행 중 입력 처리
  if (currentMode == SCREEN_GAME_RUNNING) {
    GameEngine::handleEncoderEvent(event);
    return;
  }

  switch (currentMode) {
  case SCREEN_MAIN_MIDI: {
    if (event == ENC_ROTATE_CW) {
      uint8_t v = AudioEngine::getMasterVolume();
      if (v < 100)
        AudioEngine::setMasterVolume(v + 1);
      char buf[16];
      snprintf(buf, sizeof(buf), "Vol: %d%%", AudioEngine::getMasterVolume());
      showToast(buf, 800);
    } else if (event == ENC_ROTATE_CCW) {
      uint8_t v = AudioEngine::getMasterVolume();
      if (v > 0)
        AudioEngine::setMasterVolume(v - 1);
      char buf[16];
      snprintf(buf, sizeof(buf), "Vol: %d%%", AudioEngine::getMasterVolume());
      showToast(buf, 800);
    } else if (event == ENC_BUTTON_CLICK) {
      currentMode = SCREEN_MENU_MAIN;
      menuIndex = 0;
    }
    break;
  }

  case SCREEN_MENU_MAIN: {
    int total = fontManagementEnabled ? 12 : 11;
    if (event == ENC_ROTATE_CW) {
      menuIndex = (menuIndex + 1) % total;
    } else if (event == ENC_ROTATE_CCW) {
      menuIndex = (menuIndex + total - 1) % total;
    } else if (event == ENC_BUTTON_CLICK) {
      int actionId = menuIndex;
      if (!fontManagementEnabled && actionId >= 2) {
        actionId += 1; // 사운드폰트 메뉴(2) 스킵 매핑
      }

      if (actionId == 0) {
        updateMidiFileList();
        midiMenuIndex = 0;
        currentMode = SCREEN_MENU_MIDI_LIBRARY;
      } else if (actionId == 1) {
        synthModeMenuIndex = (MIDIParser::getSynthPolicy() == SYNTH_POLICY_AUTO) ? 0 : 1;
        currentMode = SCREEN_MENU_SYNTH_MODE;
      } else if (actionId == 2) {
        updateFontFileList();
        sfMenuIndex = 0;
        currentMode = SCREEN_MENU_SOUNDFONT;
      } else if (actionId == 3) {
        currentMode = SCREEN_MENU_WIFI_INFO;
      } else if (actionId == 4) {
        baudMenuIndex = 0;
        currentMode = SCREEN_MENU_BAUDRATE;
      } else if (actionId == 5) {
        audioTestMenuIndex = 0;
        currentMode = SCREEN_MENU_AUDIO_TEST;
      } else if (actionId == 6) {
        audioOutputMenuIndex = AudioEngine::isMonoMode() ? 1 : 0;
        currentMode = SCREEN_MENU_AUDIO_OUTPUT;
      } else if (actionId == 7) {
        ledStatusMenuIndex = LEDIndicator::isEnabled() ? 0 : 1;
        currentMode = SCREEN_MENU_LED_STATUS;
      } else if (actionId == 8) {
        languageMenuIndex = isKorean ? 0 : 1;
        currentMode = SCREEN_MENU_LANGUAGE;
      } else if (actionId == 9) {
        AudioEngine::panic();
        showToast(isKorean ? "초기화 완료!" : "MIDI PANIC!");
        currentMode = SCREEN_MAIN_MIDI;
      } else if (actionId == 10) {
        aboutScrollOffset = 0;
        currentMode = SCREEN_MENU_ABOUT;
      } else if (actionId == 11) {
        currentMode = SCREEN_MAIN_MIDI;
      }
    }
    break;
  }

  case SCREEN_MENU_SYNTH_MODE: {
    if (event == ENC_ROTATE_CW) {
      synthModeMenuIndex = (synthModeMenuIndex + 1) % 3;
    } else if (event == ENC_ROTATE_CCW) {
      synthModeMenuIndex = (synthModeMenuIndex + 2) % 3;
    } else if (event == ENC_BUTTON_CLICK) {
      if (synthModeMenuIndex == 0) {
        bool isPlaying = (MIDISequencer::getState() == SEQ_PLAYING) || MIDIParser::isMIDIActive();
        if (isPlaying) {
          showToast(isKorean ? "재생중 모드 변경 불가" : "Playing: Locked", 1500);
          return;
        }
        MIDIParser::setSynthPolicy(SYNTH_POLICY_AUTO, true);
        showToast(isKorean ? "스마트 모드 적용" : "Smart Mode Set", 1200);
        currentMode = SCREEN_MAIN_MIDI;
      } else if (synthModeMenuIndex == 1) {
        manualModeMenuIndex = (int)MIDIParser::getManualSubMode();
        currentMode = SCREEN_MENU_MANUAL_MODE;
      } else {
        currentMode = SCREEN_MENU_MAIN;
      }
    }
    break;
  }

  case SCREEN_MENU_MANUAL_MODE: {
    if (event == ENC_ROTATE_CW) {
      manualModeMenuIndex = (manualModeMenuIndex + 1) % 4;
    } else if (event == ENC_ROTATE_CCW) {
      manualModeMenuIndex = (manualModeMenuIndex + 3) % 4;
    } else if (event == ENC_BUTTON_CLICK) {
      if (manualModeMenuIndex <= 2) {
        bool isPlaying = (MIDISequencer::getState() == SEQ_PLAYING) || MIDIParser::isMIDIActive();
        if (isPlaying) {
          showToast(isKorean ? "재생중 모드 변경 불가" : "Playing: Locked", 1500);
          return;
        }
      }
      if (manualModeMenuIndex == 0) {
        MIDIParser::setSynthPolicy(SYNTH_POLICY_MANUAL, true);
        MIDIParser::setManualSubMode(MANUAL_MODE_GM, true);
        showToast(isKorean ? "수동: GM 모드" : "Manual: GM", 1200);
        currentMode = SCREEN_MAIN_MIDI;
      } else if (manualModeMenuIndex == 1) {
        MIDIParser::setSynthPolicy(SYNTH_POLICY_MANUAL, true);
        MIDIParser::setManualSubMode(MANUAL_MODE_GS, true);
        showToast(isKorean ? "수동: GS 모드" : "Manual: GS", 1200);
        currentMode = SCREEN_MAIN_MIDI;
      } else if (manualModeMenuIndex == 2) {
        MIDIParser::setSynthPolicy(SYNTH_POLICY_MANUAL, true);
        MIDIParser::setManualSubMode(MANUAL_MODE_MT32, true);
        showToast(isKorean ? "수동: MT-32 모드" : "Manual: MT-32", 1200);
        currentMode = SCREEN_MAIN_MIDI;
      } else {
        currentMode = SCREEN_MENU_SYNTH_MODE;
      }
    }
    break;
  }

  case SCREEN_MENU_MIDI_LIBRARY: {
    int total = midiFileList.size() + 1; // 0번은 < Back >
    if (event == ENC_ROTATE_CW) {
      midiMenuIndex = (midiMenuIndex + 1) % total;
    } else if (event == ENC_ROTATE_CCW) {
      midiMenuIndex = (midiMenuIndex + total - 1) % total;
    } else if (event == ENC_BUTTON_CLICK) {
      if (midiMenuIndex == 0) {
        currentMode = SCREEN_MENU_MAIN;
      } else {
        String selName = midiFileList[midiMenuIndex - 1];
        String currentSong = MIDISequencer::getCurrentSongName();
        if (currentSong.startsWith("/"))
          currentSong = currentSong.substring(1);
        if (MIDISequencer::getState() == SEQ_PLAYING &&
            selName == currentSong) {
          MIDISequencer::stop();
          showToast(isKorean ? "정지됨" : "Stopped", 1200);
        } else {
          String fullPath = "/" + selName;
          if (MIDISequencer::loadFile(fullPath.c_str())) {
            MIDISequencer::play();
            showToast(isKorean ? "재생 중..." : "Playing...", 1200);
            currentMode = SCREEN_MAIN_MIDI;
          } else {
            showToast(isKorean ? "오류: 손상된 MIDI" : "Err: Bad MIDI", 1500);
          }
        }
      }
    }
    break;
  }

  case SCREEN_MENU_SOUNDFONT: {
    int total = fontFileList.size() + 1; // 0번은 < Back >
    if (event == ENC_ROTATE_CW) {
      sfMenuIndex = (sfMenuIndex + 1) % total;
    } else if (event == ENC_ROTATE_CCW) {
      sfMenuIndex = (sfMenuIndex + total - 1) % total;
    } else if (event == ENC_BUTTON_CLICK) {
      if (sfMenuIndex == 0) {
        currentMode = SCREEN_MENU_MAIN;
      } else {
        if (MIDISequencer::getState() == SEQ_PLAYING ||
            AudioEngine::getActiveVoiceCount() > 0) {
          showToast(isKorean ? "먼저 재생을 멈추십시오!" : "Stop Music First!",
                    2000);
        } else {
          AudioEngine::loadSoundFontAsync(
              fontFileList[sfMenuIndex - 1].c_str());
          showToast(isKorean ? "폰트 로딩중..." : "Font Loading...", 1500);
          currentMode = SCREEN_MAIN_MIDI;
        }
      }
    }
    break;
  }

  case SCREEN_MENU_WIFI_INFO: {
    if (event == ENC_BUTTON_CLICK) {
      currentMode = SCREEN_MENU_MAIN;
    }
    break;
  }

  case SCREEN_MENU_BAUDRATE: {
    if (event == ENC_ROTATE_CW) {
      baudMenuIndex = (baudMenuIndex + 1) % 4;
    } else if (event == ENC_ROTATE_CCW) {
      baudMenuIndex = (baudMenuIndex + 3) % 4;
    } else if (event == ENC_BUTTON_CLICK) {
      if (baudMenuIndex == 0) {
        MIDIParser::setBaudRate(38400);
        showToast(isKorean ? "통신속도: 38400" : "Baud: 38400");
        currentMode = SCREEN_MAIN_MIDI;
      } else if (baudMenuIndex == 1) {
        MIDIParser::setBaudRate(31250);
        showToast(isKorean ? "통신속도: 31250" : "Baud: 31250");
        currentMode = SCREEN_MAIN_MIDI;
      } else if (baudMenuIndex == 2) {
        MIDIParser::setBaudRate(115200);
        showToast(isKorean ? "통신속도: 115200" : "Baud: 115200");
        currentMode = SCREEN_MAIN_MIDI;
      } else if (baudMenuIndex == 3) {
        currentMode = SCREEN_MENU_MAIN;
      }
    }
    break;
  }

  case SCREEN_MENU_AUDIO_TEST: {
    int total = 5;
    if (event == ENC_ROTATE_CW) {
      audioTestMenuIndex = (audioTestMenuIndex + 1) % total;
    } else if (event == ENC_ROTATE_CCW) {
      audioTestMenuIndex = (audioTestMenuIndex + total - 1) % total;
    } else if (event == ENC_BUTTON_CLICK) {
      if (audioTestMenuIndex == 0) {
        AudioEngine::playTestSound(1);
        showToast(isKorean ? "피아노 화음 재생" : "Piano Playing...", 1500);
      } else if (audioTestMenuIndex == 1) {
        AudioEngine::playTestSound(2);
        showToast(isKorean ? "기타 아르페지오 재생" : "Guitar Playing...",
                  1500);
      } else if (audioTestMenuIndex == 2) {
        AudioEngine::playTestSound(3);
        showToast(isKorean ? "드럼 키트 재생" : "Drums Playing...", 1500);
      } else if (audioTestMenuIndex == 3) {
        AudioEngine::playTestSound(4);
      } else if (audioTestMenuIndex == 4) {
        currentMode = SCREEN_MENU_MAIN;
      }
    }
    break;
  }

  case SCREEN_MENU_AUDIO_OUTPUT: {
    int total = 3;
    if (event == ENC_ROTATE_CW) {
      audioOutputMenuIndex = (audioOutputMenuIndex + 1) % total;
    } else if (event == ENC_ROTATE_CCW) {
      audioOutputMenuIndex = (audioOutputMenuIndex + total - 1) % total;
    } else if (event == ENC_BUTTON_CLICK) {
      if (audioOutputMenuIndex == 0) {
        AudioEngine::setMonoMode(false);
        showToast(isKorean ? "스테레오 출력 설정" : "Audio: Stereo", 1200);
        currentMode = SCREEN_MENU_MAIN;
      } else if (audioOutputMenuIndex == 1) {
        AudioEngine::setMonoMode(true);
        showToast(isKorean ? "모노 출력 설정" : "Audio: Mono", 1200);
        currentMode = SCREEN_MENU_MAIN;
      } else if (audioOutputMenuIndex == 2) {
        currentMode = SCREEN_MENU_MAIN;
      }
    }
    break;
  }

  case SCREEN_MENU_LED_STATUS: {
    int total = 3;
    if (event == ENC_ROTATE_CW) {
      ledStatusMenuIndex = (ledStatusMenuIndex + 1) % total;
    } else if (event == ENC_ROTATE_CCW) {
      ledStatusMenuIndex = (ledStatusMenuIndex + total - 1) % total;
    } else if (event == ENC_BUTTON_CLICK) {
      if (ledStatusMenuIndex == 0) {
        LEDIndicator::setEnabled(true);
        showToast(isKorean ? "LED 표시등 켜짐" : "LED: Enabled", 1200);
        currentMode = SCREEN_MENU_MAIN;
      } else if (ledStatusMenuIndex == 1) {
        LEDIndicator::setEnabled(false);
        showToast(isKorean ? "LED 표시등 꺼짐" : "LED: Disabled", 1200);
        currentMode = SCREEN_MENU_MAIN;
      } else if (ledStatusMenuIndex == 2) {
        currentMode = SCREEN_MENU_MAIN;
      }
    }
    break;
  }

  case SCREEN_MENU_LANGUAGE: {
    int total = 3;
    if (event == ENC_ROTATE_CW) {
      languageMenuIndex = (languageMenuIndex + 1) % total;
    } else if (event == ENC_ROTATE_CCW) {
      languageMenuIndex = (languageMenuIndex + total - 1) % total;
    } else if (event == ENC_BUTTON_CLICK) {
      if (languageMenuIndex == 0) {
        setKoreanMode(true);
        showToast("한국어로 설정됨", 1200);
        currentMode = SCREEN_MENU_MAIN;
      } else if (languageMenuIndex == 1) {
        setKoreanMode(false);
        showToast("Language: English", 1200);
        currentMode = SCREEN_MENU_MAIN;
      } else if (languageMenuIndex == 2) {
        currentMode = SCREEN_MENU_MAIN;
      }
    }
    break;
  }

  case SCREEN_MENU_ABOUT: {
    int totalLines = 18;
    int visibleLines = 4;
    int maxScroll = totalLines - visibleLines;
    if (event == ENC_ROTATE_CW) {
      if (aboutScrollOffset < maxScroll)
        aboutScrollOffset++;
    } else if (event == ENC_ROTATE_CCW) {
      if (aboutScrollOffset > 0)
        aboutScrollOffset--;
    } else if (event == ENC_BUTTON_CLICK) {
      currentMode = SCREEN_MENU_MAIN;
    }
    break;
  }

    case SCREEN_MENU_GAMES: {
        int total = 6;
        if (event == ENC_ROTATE_CW) {
            gamesMenuIndex = (gamesMenuIndex + 1) % total;
        } else if (event == ENC_ROTATE_CCW) {
            gamesMenuIndex = (gamesMenuIndex + total - 1) % total;
        } else if (event == ENC_BUTTON_CLICK) {
            if (gamesMenuIndex < 5) { // 5개 게임(0~4)에 대해서만 MIDI 재생 중 잠금
                bool isMidiPlaying = (MIDISequencer::getState() == SEQ_PLAYING && !MIDISequencer::isLoopEnabled()) || MIDIParser::isMIDIActive();
                if (isMidiPlaying) {
                    showToast(isKorean ? "MIDI 연주 중 진입 불가" : "MIDI Playing: Locked", 1500);
                    return;
                }
            }
            if (gamesMenuIndex == 0) {
        GameEngine::init(GAME_PIANO);
        currentMode = SCREEN_GAME_RUNNING;
      } else if (gamesMenuIndex == 1) {
        GameEngine::init(GAME_PONG);
        currentMode = SCREEN_GAME_RUNNING;
      } else if (gamesMenuIndex == 2) {
        GameEngine::init(GAME_BLOCK_STACK);
        currentMode = SCREEN_GAME_RUNNING;
      } else if (gamesMenuIndex == 3) {
        GameEngine::init(GAME_BRICK);
        currentMode = SCREEN_GAME_RUNNING;
      } else if (gamesMenuIndex == 4) {
        GameEngine::init(GAME_SNAKE);
        currentMode = SCREEN_GAME_RUNNING;
      } else if (gamesMenuIndex == 5) {
        currentMode = SCREEN_MAIN_MIDI;
      }
    }
    break;
  }
  }
}

void DisplayUI::update() {
  if (!oledAvailable)
    return; // OLED 미연결 시 스킵

  unsigned long now = millis();

  // 1. 게임 실행 중일 때: 60 FPS (16ms) 고주사율 렌더링
  if (currentMode == SCREEN_GAME_RUNNING) {
    wakeup();
    static unsigned long lastGameRenderTime = 0;
    if (now - lastGameRenderTime < 16)
      return; // 60 FPS
    lastGameRenderTime = now;

    u8g2.clearBuffer();
    GameEngine::update(u8g2);
    drawToast();
    u8g2.sendBuffer();
    return;
  }

  // 2. 일반 화면: 5분(300,000ms) 이상 무음 및 입력 없을 시 OLED 절전
  bool isPlaying = (MIDISequencer::getState() == SEQ_PLAYING ||
                    AudioEngine::getActiveVoiceCount() > 0);
  if (isPlaying) {
    wakeup();
  } else if (!isSleeping && (now - lastActivityTime >= 300000)) {
    u8g2.setPowerSave(1);
    isSleeping = true;
    return;
  }

  if (isSleeping)
    return; // 절전 중일 때는 렌더링 스킵

  static unsigned long lastRenderTime = 0;
  if (now - lastRenderTime < 125)
    return; // 8 FPS (125ms 주기 - 800kHz I2C & 512 버퍼 최적화로 부드러운
            // 화면과 저부하 동시 실현)
  lastRenderTime = now;

  // 8초간 메뉴에서 입력 없으면 메인 화면 자동 복귀 (단, 게임 메뉴 제외)
  if (currentMode != SCREEN_MAIN_MIDI && currentMode != SCREEN_MENU_GAMES &&
      (now - lastInteractionTime > 8000)) {
    currentMode = SCREEN_MAIN_MIDI;
  }

  u8g2.clearBuffer();

  switch (currentMode) {
  case SCREEN_MAIN_MIDI:
    drawMainMIDIScreen();
    break;
  case SCREEN_MENU_MAIN:
    drawMainMenu();
    break;
  case SCREEN_MENU_MIDI_LIBRARY:
    drawMIDILibraryMenu();
    break;
  case SCREEN_MENU_SYNTH_MODE:
    drawSynthModeMenu();
    break;
  case SCREEN_MENU_MANUAL_MODE:
    drawManualModeMenu();
    break;
  case SCREEN_MENU_SOUNDFONT:
    drawSoundFontMenu();
    break;
  case SCREEN_MENU_WIFI_INFO:
    drawWiFiInfoMenu();
    break;
  case SCREEN_MENU_BAUDRATE:
    drawBaudRateMenu();
    break;
  case SCREEN_MENU_AUDIO_TEST:
    drawAudioTestMenu();
    break;
  case SCREEN_MENU_AUDIO_OUTPUT:
    drawAudioOutputMenu();
    break;
  case SCREEN_MENU_LED_STATUS:
    drawLEDStatusMenu();
    break;
  case SCREEN_MENU_LANGUAGE:
    drawLanguageMenu();
    break;
  case SCREEN_MENU_ABOUT:
    drawAboutMenu();
    break;
  case SCREEN_MENU_GAMES:
    drawGamesMenu();
    break;
  }

  drawToast();
  u8g2.sendBuffer();
}
