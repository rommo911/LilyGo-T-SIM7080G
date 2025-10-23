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
    bool setupPower();

    void getWakeupReason();

    extern XPowersPMU PMU;
};
