#pragma once

#include <Arduino.h>

class WebManager {
public:
    static void begin();
    static bool isFontManagementEnabled();
    static void setFontManagementEnabled(bool enabled);
};
