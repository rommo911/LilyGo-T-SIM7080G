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
        TIMER = 2,
        UNKNOWN
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
    void DeepSleepWith_IMU_Timer_Wake(uint32_t ms);
    void DeepSleepWith_Timer_Wake(uint32_t ms);
};

// helper: map 0..100% to a red->yellow->green gradient
static CRGB batteryColor(uint8_t percent)
{
    if (percent >= 90)
    {
        return CRGB(0, 50, 0);
    }
    if (percent <= 10)
    {
        return CRGB(50, 0, 0);
    }
    // map both red and green to 0..50 and make them complementary so
    // r + g == 50 (max). This keeps a smooth red->green gradient while
    // ensuring the sum never exceeds 50.
    uint8_t r = (uint8_t)(((uint16_t)(100 - percent) * 50) / 100);
    uint8_t g = (uint8_t)(((uint16_t)percent * 50) / 100);
    return CRGB(r, g, 0);
}