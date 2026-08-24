#pragma once

#include <Arduino.h>

class TimeManager {
public:
    static void begin();
    static void update();
    
    // 시간 문자열 반환 ("AM 12:34", "PM 03:20", 미설정 시 "--:--")
    static String getFormattedTime();
    
    // 설정 가져오기
    static bool isAutoNTP();
    static int getUTCOffset();
    static String getNTPServer();
    
    // 설정 저장
    static void setConfig(bool autoNTP, int utcOffset, const String& ntpServer);
    static void setManualTime(int year, int month, int day, int hour, int min, int sec);
    
    // 사전 정의된 7대 공인 NTP 서버 목록
    static const char* const NTP_SERVERS[7];
    static const int NTP_SERVER_COUNT = 7;

private:
    static bool autoNTP;
    static int utcOffset;
    static String ntpServer;
    static bool timeSynced;
    static unsigned long lastNTPAttempt;
    
    static void syncNTP();
};
