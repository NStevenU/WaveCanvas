#include "time_manager.h"
#include <WiFi.h>
#include <Preferences.h>
#include <time.h>
#include <sys/time.h>

const char* const TimeManager::NTP_SERVERS[7] = {
    "pool.ntp.org",
    "time.google.com",
    "time.cloudflare.com",
    "time.windows.com",
    "time.apple.com",
    "ntp.kriss.re.kr",
    "time.nist.gov"
};

bool TimeManager::autoNTP = true;
int TimeManager::utcOffset = 9; // 기본값: 한국/일본 (UTC+9)
String TimeManager::ntpServer = "pool.ntp.org";
bool TimeManager::timeSynced = false;
unsigned long TimeManager::lastNTPAttempt = 0;

void TimeManager::begin() {
    Preferences prefs;
    prefs.begin("time_cfg", true);
    autoNTP = prefs.getBool("auto_ntp", true);
    utcOffset = prefs.getInt("utc_off", 9);
    ntpServer = prefs.getString("ntp_srv", "pool.ntp.org");
    prefs.end();

    if (autoNTP) {
        lastNTPAttempt = millis();
        syncNTP();
    }
}

void TimeManager::syncNTP() {
    if (WiFi.status() == WL_CONNECTED) {
        configTime(utcOffset * 3600, 0, ntpServer.c_str());
    }
}

void TimeManager::update() {
    if (autoNTP && WiFi.status() == WL_CONNECTED) {
        if (!timeSynced) {
            time_t now = 0;
            time(&now);
            if (now > 1700000000) { // 2023년 11월 이후 정상 동기화된 에포크 시각 확인
                timeSynced = true;
            } else if (millis() - lastNTPAttempt > 8000) {
                lastNTPAttempt = millis();
                syncNTP();
            }
        }
    }
}

String TimeManager::getFormattedTime() {
    if (!timeSynced) {
        return "--:--";
    }

    time_t now;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    int hour = timeinfo.tm_hour;
    int minute = timeinfo.tm_min;
    bool isPM = (hour >= 12);
    int displayHour = hour % 12;
    if (displayHour == 0) displayHour = 12;

    char buf[16];
    snprintf(buf, sizeof(buf), "%s %02d:%02d", isPM ? "PM" : "AM", displayHour, minute);
    return String(buf);
}

bool TimeManager::isAutoNTP() { return autoNTP; }
int TimeManager::getUTCOffset() { return utcOffset; }
String TimeManager::getNTPServer() { return ntpServer; }

void TimeManager::setConfig(bool auto_ntp, int utc_off, const String& ntp_srv) {
    autoNTP = auto_ntp;
    utcOffset = utc_off;
    ntpServer = ntp_srv;

    Preferences prefs;
    prefs.begin("time_cfg", false);
    prefs.putBool("auto_ntp", autoNTP);
    prefs.putInt("utc_off", utcOffset);
    prefs.putString("ntp_srv", ntpServer);
    prefs.end();

    timeSynced = false;
    if (autoNTP) {
        syncNTP();
    }
}

void TimeManager::setManualTime(int year, int month, int day, int hour, int min, int sec) {
    struct tm t;
    t.tm_year = year - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min = min;
    t.tm_sec = sec;
    t.tm_isdst = 0;

    time_t epoch = mktime(&t);
    struct timeval tv = { .tv_sec = epoch, .tv_usec = 0 };
    settimeofday(&tv, NULL);
    timeSynced = true;

    // 수동 시간 설정 시 자동 NTP 비활성화
    autoNTP = false;
    Preferences prefs;
    prefs.begin("time_cfg", false);
    prefs.putBool("auto_ntp", false);
    prefs.end();

}
