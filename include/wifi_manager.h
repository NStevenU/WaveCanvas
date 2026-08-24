#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <vector>

struct WiFiScanResult {
    String ssid;
    int32_t rssi;
    bool isEncrypted;
};

class WiFiManager {
public:
    static void begin();
    static bool isAPMode();
    static const char* getModeString();
    static const char* getSSID();
    static String getIPAddress();

    static void triggerScan();            // 비동기 백그라운드 스캔 시작
    static bool isScanning();             // 스캔 진행 중 여부
    static std::vector<WiFiScanResult> getCachedScanResults(); // 캐시된 결과
    static void setCachedResults(const std::vector<WiFiScanResult>& results);
    static void setScanningActive(bool active);
    
    static bool saveAndConnect(const String& ssid, const String& password);
    static void connectAsync(const String& ssid, const String& password);
    static void startSoftAP();

private:
    static bool apModeActive;
    static String currentSSID;
    static String currentIP;
    static std::vector<WiFiScanResult> cachedResults;
    static bool scanningActive;
};
