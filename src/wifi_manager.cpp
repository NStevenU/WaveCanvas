#include "wifi_manager.h"
#include "config.h"
#include "led_indicator.h"
#include <Preferences.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

static SemaphoreHandle_t s_wifiMutex = nullptr;
bool WiFiManager::apModeActive = false;
String WiFiManager::currentSSID = DEFAULT_AP_SSID;
String WiFiManager::currentIP = "192.168.4.1";
std::vector<WiFiScanResult> WiFiManager::cachedResults;
bool WiFiManager::scanningActive = false;

void WiFiManager::startSoftAP() {
    apModeActive = true;
    WiFi.mode(WIFI_AP_STA); // AP_STA 모드: AP 작동 중에도 STA 라디오 활성 → 스캔 가능
    WiFi.softAP(DEFAULT_AP_SSID, DEFAULT_AP_PASS);
    currentSSID = DEFAULT_AP_SSID;
    currentIP = WiFi.softAPIP().toString();

    LEDIndicator::setState(LED_AP_MODE);
}

void WiFiManager::begin() {
    if (!s_wifiMutex) {
        s_wifiMutex = xSemaphoreCreateMutex();
    }
    Preferences prefs;
    prefs.begin("wifi_cfg", true);
    String savedSSID = prefs.getString("ssid", "");
    String savedPass = prefs.getString("pass", "");
    prefs.end();

    if (savedSSID.length() > 0) {
        LEDIndicator::setState(LED_WIFI_CONNECTING);

        WiFi.mode(WIFI_STA);
        WiFi.begin(savedSSID.c_str(), savedPass.c_str());

        unsigned long start = millis();
        bool connected = false;

        // 빠른 부팅을 위해 최대 3.5초 대기 (공유기 부재 시 지체 없이 SoftAP로 즉시 전환)
        while (millis() - start < 3500) {
            LEDIndicator::update();
            if (WiFi.status() == WL_CONNECTED) {
                connected = true;
                break;
            }
            delay(50);
        }

        if (connected) {
            apModeActive = false;
            currentSSID = savedSSID;
            currentIP = WiFi.localIP().toString();
            LEDIndicator::setState(LED_NORMAL);
            return;
        } else {
        }
    }

    startSoftAP();
}

bool WiFiManager::isAPMode() {
    return apModeActive;
}

const char* WiFiManager::getModeString() {
    return apModeActive ? "SoftAP" : "STA";
}

const char* WiFiManager::getSSID() {
    return currentSSID.c_str();
}

String WiFiManager::getIPAddress() {
    return currentIP;
}

std::vector<WiFiScanResult> WiFiManager::getCachedScanResults() {
    std::vector<WiFiScanResult> res;
    if (s_wifiMutex && xSemaphoreTake(s_wifiMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        res = cachedResults;
        xSemaphoreGive(s_wifiMutex);
    } else {
        res = cachedResults;
    }
    return res;
}

bool WiFiManager::isScanning() {
    bool scanning = false;
    if (s_wifiMutex && xSemaphoreTake(s_wifiMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        scanning = scanningActive;
        xSemaphoreGive(s_wifiMutex);
    } else {
        scanning = scanningActive;
    }
    return scanning;
}

// FreeRTOS 태스크로 백그라운드에서 Wi-Fi 스캔 실행
// AsyncWebServer 핸들러 안에서 직접 호출하면 TCP 스택이 멈춰 크래시 발생
static void wifiScanTask(void* param) {
    // AP_STA 모드 보장 (STA 라디오 활성 상태에서만 스캔 가능)
    if (WiFi.getMode() == WIFI_AP) {
        WiFi.mode(WIFI_AP_STA);
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    // 동기 스캔 (이 태스크는 별도 FreeRTOS 스레드이므로 블로킹해도 안전)
    int16_t n = WiFi.scanNetworks(false, true);
    
    std::vector<WiFiScanResult> results;
    if (n > 0) {
        for (int i = 0; i < n; i++) {
            WiFiScanResult res;
            res.ssid = WiFi.SSID(i);
            res.rssi = WiFi.RSSI(i);
            res.isEncrypted = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
            if (res.ssid.length() > 0) {
                results.push_back(res);
            }
        }
        WiFi.scanDelete();
    }
    
    WiFiManager::setCachedResults(results);
    WiFiManager::setScanningActive(false);
    
    vTaskDelete(NULL); // 태스크 자체 종료
}

void WiFiManager::triggerScan() {
    bool shouldStart = false;
    if (s_wifiMutex && xSemaphoreTake(s_wifiMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        if (!scanningActive) {
            scanningActive = true;
            shouldStart = true;
        }
        xSemaphoreGive(s_wifiMutex);
    } else if (!scanningActive) {
        scanningActive = true;
        shouldStart = true;
    }
    if (!shouldStart) return; // 이미 스캔 중이면 무시
    
    // 별도 FreeRTOS 태스크로 스캔 실행 (4KB 스택, Core 0)
    xTaskCreatePinnedToCore(
        wifiScanTask,
        "WiFiScan",
        4096,
        NULL,
        1,    // 낮은 우선순위
        NULL,
        0     // Core 0 (네트워크 코어)
    );
}

void WiFiManager::setCachedResults(const std::vector<WiFiScanResult>& results) {
    if (s_wifiMutex && xSemaphoreTake(s_wifiMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        cachedResults = results;
        xSemaphoreGive(s_wifiMutex);
    } else {
        cachedResults = results;
    }
}

void WiFiManager::setScanningActive(bool active) {
    if (s_wifiMutex && xSemaphoreTake(s_wifiMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        scanningActive = active;
        xSemaphoreGive(s_wifiMutex);
    } else {
        scanningActive = active;
    }
}

bool WiFiManager::saveAndConnect(const String& ssid, const String& password) {
    Preferences prefs;
    prefs.begin("wifi_cfg", false);
    prefs.putString("ssid", ssid);
    prefs.putString("pass", password);
    prefs.end();

    LEDIndicator::setState(LED_WIFI_CONNECTING);

    WiFi.disconnect();
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), password.c_str());

    unsigned long start = millis();
    bool connected = false;

    while (millis() - start < 10000) {
        LEDIndicator::update();
        if (WiFi.status() == WL_CONNECTED) {
            connected = true;
            break;
        }
        delay(50);
    }

    if (connected) {
        apModeActive = false;
        currentSSID = ssid;
        currentIP = WiFi.localIP().toString();
        LEDIndicator::setState(LED_NORMAL);
        return true;
    } else {
        startSoftAP();
        return false;
    }
}

struct WiFiAsyncParams {
    String ssid;
    String pass;
};

static void asyncConnectTask(void* param) {
    WiFiAsyncParams* p = (WiFiAsyncParams*)param;
    vTaskDelay(pdMS_TO_TICKS(1500)); // HTTP 응답 전송 대기
    if (p) {
        WiFiManager::saveAndConnect(p->ssid, p->pass);
        delete p;
    }
    vTaskDelete(NULL);
}

void WiFiManager::connectAsync(const String& ssid, const String& password) {
    WiFiAsyncParams* p = new WiFiAsyncParams{ssid, password};
    xTaskCreatePinnedToCore(asyncConnectTask, "WiFiConn", 4096, p, 1, NULL, 0);
}
