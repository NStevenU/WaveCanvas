#pragma once

#include <Arduino.h>
#include "encoder_input.h"

enum ScreenMode {
    SCREEN_MAIN_MIDI,
    SCREEN_MENU_MAIN,
    SCREEN_MENU_MIDI_LIBRARY,
    SCREEN_MENU_SOUNDFONT,
    SCREEN_MENU_WIFI_INFO,
    SCREEN_MENU_BAUDRATE,
    SCREEN_MENU_AUDIO_TEST,
    SCREEN_MENU_AUDIO_OUTPUT,
    SCREEN_MENU_LED_STATUS,
    SCREEN_MENU_LANGUAGE,
    SCREEN_MENU_ABOUT,
    SCREEN_MENU_GAMES,
    SCREEN_GAME_RUNNING
};

class DisplayUI {
public:
    static void begin();
    static void update();
    static void handleEncoderEvent(EncoderEvent event);
    static void showToast(const char* message, uint16_t durationMs = 1500);
    static void wakeup();
    static void resetChannelDisplay();
    static void onExternalMIDIActivity();
    static void invalidateFileListCache();
    static ScreenMode getMode();
    static bool isKoreanMode();
    static void setKoreanMode(bool ko);
    static void setFontManagementEnabled(bool enabled);
    static bool isFontManagementEnabled();

private:
    static ScreenMode currentMode;
    static bool fontManagementEnabled;
    static int menuIndex;
    static int midiMenuIndex;
    static int sfMenuIndex;
    static int baudMenuIndex;
    static int audioTestMenuIndex;
    static int audioOutputMenuIndex;
    static int ledStatusMenuIndex;
    static int languageMenuIndex;
    static int gamesMenuIndex;
    static int aboutScrollOffset;
    static bool isKorean;
    static unsigned long lastInteractionTime;
    static unsigned long lastActivityTime;
    static bool isSleeping;
    static char toastMsg[48];
    static unsigned long toastEndTime;

    static void drawMainMIDIScreen();
    static void drawMainMenu();
    static void drawMIDILibraryMenu();
    static void drawSoundFontMenu();
    static void drawWiFiInfoMenu();
    static void drawBaudRateMenu();
    static void drawAudioTestMenu();
    static void drawAudioOutputMenu();
    static void drawLEDStatusMenu();
    static void drawLanguageMenu();
    static void drawAboutMenu();
    static void drawGamesMenu();
    static void drawScrollbar(int x, int y, int w, int h, int totalItems, int currentIndex, int visibleItems);
    static void drawToast();
    static void updateMidiFileList();
};
