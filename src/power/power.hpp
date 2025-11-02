/**
 * @file      power.h
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2022  Shenzhen Xin Yuan Electronic Technology Co., Ltd
 * @date      2022-09-16
 *
 */

#define XPOWERS_CHIP_AXP2101
#include "XPowersLib.h"
#include "FastLED.h"

namespace power
{
    enum WakeUpReason : uint8_t
    {
        MOTION = 0,
        START = 1,
        UNKNOWN = 2
    };
    bool setupPower();
    esp_sleep_wakeup_cause_t getWakeupReason();
    WakeUpReason Get_wake_reason();

    XPowersPMU &getPMU();
    bool isBattCharging();
    bool isPowerVBUSOn();
    bool isBatLowLevel();
    bool isBatCriticalLevel();
    bool iskeyShortPressed();
    void DeepSleepWith_IMU_PMU_Wake();
    void DeepSleepWith_PMU_Wake();
    uint64_t getLastVbusInsertedTs();
    uint64_t getLastVbusRemovedTs();

};

// helper: map 0..100% to a red->yellow->green gradient
static CRGB batteryColor(uint8_t percent)
{
    if (percent > 100)
        percent = 100;
    uint8_t r = 0, g = 0;
    if (percent <= 50)
    {
        // red -> yellow (increase green)
        r = 255;
        g = (uint8_t)((uint16_t)percent * 255 / 50); // 0..255
    }
    else
    {
        // yellow -> green (decrease red)
        g = 255;
        r = (uint8_t)((uint16_t)(100 - percent) * 255 / 50); // 255..0
    }
    return CRGB(r, g, 0);
}