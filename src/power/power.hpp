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
    XPowersPMU &getPMU();
    bool isBattCharging();
    bool isPowerVBUSOn();
    bool isBatLowLevel();
    bool isBatCriticalLevel();
    bool iskeyShortPressed();
    void DeepSleepWith_IMU_PMU_Wake();
    void DeepSleepWith_PMU_Wake();

};
