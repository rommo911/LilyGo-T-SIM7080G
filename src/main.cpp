
/**
 * @file      AllFunction.ino
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2022  Shenzhen Xin Yuan Electronic Technology Co., Ltd
 * @date      2022-09-16
 *
 */
#include <Arduino.h>
#include "sdcard/sdcard.h"
#include "wifi/wifi.hpp"
#include "power/power.hpp"
#include "pins.hpp"
#include "main.hpp"
#include "fast_led/fast_led.hpp"
// #include "imu/imu_DMP6.hpp"
#include "imu6500/imu_DMP6.hpp"
#include "wifi/wifi.hpp"
#include "modem/modem.hpp"
static const uint32_t WifiTimeout = 1000U * 60U * 5U;
static const uint32_t No_MotionTimeout = 30U * 1000U; // 30 seconds
imu6500_dmp::MotionDtect_t motion = {};
static bool carEverStarted = false;

void setup()
{
    bool ret = false;
    Serial.begin(115200);
    uint8_t counter = 0;
    power::setupPower();
    fast_led::fast_led_init();
    pinMode(CAM_PIN, OUTPUT);
    digitalWrite(CAM_PIN, HIGH);
    while (!Serial && counter++ < 20)
    {
        delay(200);
    };
    delay(5000);
    power::getWakeupReason();
    modem::shutdownModem();

    // StartWifi();

    if (imu6500_dmp::imu_setup())
    {
        mqttLogger.println("IMU setup complete ");
    }
    else
    {
        mqttLogger.println("IMU setup failed ");
        fast_led::start_blink(0, CRGB::Red);
        delay(5000);
        fast_led::stop_led(0);
        delay(15);
        ESP.restart();
    }

    /*bool modRet = modem::initModem7080();
    if (modRet)
    {
        modem::setRF(false);
        modem::SetGPS(true);
    }*/
    fast_led::set_solid(0, {0, 0, 50});
    // fast_led::start_blink(1, {0, 0, 50}, 200, 450, 3);
    // delay(5000);
    // fast_led::start_fade(1, {0, 0, 50}, CRGB::Red, 500);
    // delay(5000);
    // fast_led::start_fade(1, {0, 50, 0}, CRGB::Black, 500, 5);
    // delay(5000);
}

static uint64_t LastWifiOnTimestamp = 0;
bool simulatedMotionTrigger = false;
bool simulatedLowPowerTrigger = false;
bool simulatedCriticalLowPowerTrigger = false;
void loopPowerCheck()
{
    carEverStarted |= power::isPowerVBUSOn();
    if (power::isBatCriticalLevel() || simulatedCriticalLowPowerTrigger)
    {
        mqttLogger.printf("Battery critical level detected in main loop %d - %d \n", power::isBatCriticalLevel() ? 1 : 0, simulatedCriticalLowPowerTrigger ? 1 : 0);
        simulatedCriticalLowPowerTrigger = false;

        imu6500_dmp::shutdown();
        power::DeepSleepWith_PMU_Wake();
    }
    if (power::isBatLowLevel() || simulatedLowPowerTrigger)
    {
        simulatedLowPowerTrigger = false;
        mqttLogger.printf("Battery low level detected in main loop %d - %d \n", power::isBatLowLevel() ? 1 : 0, simulatedLowPowerTrigger ? 1 : 0);
        power::DeepSleepWith_IMU_PMU_Wake();
    }
}

static uint64_t no_motion_debounce = 0;
static uint64_t motion_Calibrate_debounce = 0;
static uint64_t ms_since_off_no_motion = 0;

void loopImuMotion()
{
    if (power::isPowerVBUSOn())
    {
        // return;
    }
    motion = imu6500_dmp::imu_get_moved();
    if ((millis() - imu6500_dmp::getLastMovedTimestamp() > No_MotionTimeout) && (no_motion_debounce++ > 100U)) // no motion for 5s and car not started
    {
        // sleep
        if (ms_since_off_no_motion == 0)
        {
            ms_since_off_no_motion = millis();
        }
        if (ms_since_off_no_motion > (millis() + (1U * 60U * 1000U)))
        {
            mqttLogger.println("been a 1 minutes with no motion CanSleep NOW??");
            digitalWrite(CAM_PIN, LOW);
        }
        no_motion_debounce = 0;
        motion_Calibrate_debounce = 0;
    }
    if ((motion || simulatedMotionTrigger) == true)
    {
        ms_since_off_no_motion = 0;
        digitalWrite(CAM_PIN, HIGH);
        Serial.println("Loop Motion detected ");
        fast_led::start_blink(0, CRGB::Blue, CRGB::Black, 75, 1000, 1);
        no_motion_debounce = 0;
    }
    if (!(motion || simulatedMotionTrigger))
    {
        no_motion_debounce = 0;
        motion_Calibrate_debounce = 0;
    }
    simulatedMotionTrigger = false;
}

void loopWifiStatus()
{
    if (power::iskeyShortPressed())
    {
        mqttLogger.println("Power key short pressed detected in main loop");
        if (!GetWifiOn())
        {
            StartWifi();
        }
        LastWifiOnTimestamp = millis();
    }
    if (GetWifiOn() && (millis() - LastWifiOnTimestamp > WifiTimeout))
    {
        mqttLogger.println("WiFi on timeout reached, turning off WiFi");
        StopWifi();
    }
}

void loop()
{
    loopWifiStatus();
    loopPowerCheck();
    // motion = imu6500_dmp::imu_get_moved();
    // if (motion)
    // {
    //     // mqttLogger.println("MPU loop Motion detected");
    // }
    loopImuMotion();
    delay(10);
}
