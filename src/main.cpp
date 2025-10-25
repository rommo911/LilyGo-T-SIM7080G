
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
#include "imu/imu_DMP6.hpp"
#include "wifi/wifi.hpp"
#include "modem/modem.hpp"

enum WakeUpReason : uint8_t
{
    MOTION = 0,
    START = 1,
    UNKNOWN = 2
};
bool carEverStarted = false;
WakeUpReason wakeUpReason = WakeUpReason::UNKNOWN;

WakeUpReason Get_wake_reason()
{
    uint64_t wakeup_pin_mask = esp_sleep_get_ext1_wakeup_status();

    wakeup_pin_mask = esp_sleep_get_ext1_wakeup_status();
    if (wakeup_pin_mask & ((uint64_t)1 << MOTION_INTRRUPT_PIN))
    {
        Serial.println(F("Wakeup cause detected: MPU motion interrupt"));
        return WakeUpReason::MOTION;
    }
    if (wakeup_pin_mask & ((uint64_t)1 << CAR_START_PIN))
    {
        Serial.println(F("Wakeup cause detected: Start button"));
        return WakeUpReason::START;
    }
    else
    {
        Serial.printf(F("Wakeup cause detected: 0x%llx\n"), wakeup_pin_mask);
    }
    return WakeUpReason::UNKNOWN;
}

void setup()
{
    bool ret = false;
    fast_led::fast_led_init();
    Serial.begin(115200);
    uint8_t counter = 0;
    power::setupPower();
    pinMode(CAM_PIN, OUTPUT);
    digitalWrite(CAM_PIN, HIGH);
    while (!Serial && counter++ < 20)
    {
        delay(200);
    };
    power::getWakeupReason();
    modem::shutdownModem();
    delay(500);
    // setUpWifiAP();

    Serial.println("=========================================");
    if (imu_dmp::imu_setup())
    {
        mqttLogger.println("IMU setup complete ");
    }
    else
    {
        mqttLogger.println("IMU setup failed ");
        // fast_led::set_fast_led(0, CRGB::Red);
        // fast_led::set_blink(true, 200);
        // uint32_t counter = 0;
        // while (counter++ < 240)
        // {
        //     delay(1000);
        // }
        // ESP.restart();
    }

    /*bool modRet = modem::initModem7080();
    if (modRet)
    {
        modem::setRF(false);
        modem::SetGPS(true);
    }*/
}

static uint64_t LastWifiOnTimestamp = 0;
bool simulatedPowerKeyTrigger = false;
bool simulatedMotionTrigger = false;
bool simulatedLowPowerTrigger = false;
bool simulatedCriticalLowPowerTrigger = false;
void loopPowerCheck()
{
    // Serial.println("Checking power status...loop");
    carEverStarted |= power::isPowerVBUSOn();

    if (power::isBatCriticalLevel())
    {
        mqttLogger.println("Battery critical level detected in main loop");
        imu_dmp::shutdown();
        power::DeepSleepWith_PMU_Wake();
    }
    if (power::isBatLowLevel())
    {
        mqttLogger.println("Battery low level detected in main loop");
        if (GetWifiOn())
        {
            StopWifi();
        }
        imu_dmp::setupLowPowerMode();
        power::DeepSleepWith_IMU_PMU_Wake();
    }
}

static uint64_t no_motion_debounce = 0;
static uint64_t motion_Calibrate_debounce = 0;
static uint64_t ms_since_off_no_motion = 0;
imu_dmp::MotionDtect_t motion = {};
const uint32_t No_MotionTimeout = 30 * 1000U; // 30 seconds

void loopImuMotion()
{
    if (!(motion || simulatedMotionTrigger) && (no_motion_debounce++ > 100U) && (millis() - imu_dmp::getLastMovedTimestamp() > No_MotionTimeout)) // no motion for 5s and car not started
    {
        // sleep
        mqttLogger.println("No motion and car off");
        digitalWrite(CAM_PIN, LOW);
        if (millis() + (3 * 60 * 1000) > imu_dmp::get_last_baseline_reset())
        {
            fast_led::set_fast_led(0, CRGB::Green);
            motion_Calibrate_debounce = 0;
            imu_dmp::resetBaseline();
            mqttLogger.println("Motion Calibrate in no motion mode");
            delay(250);
        }
        if (ms_since_off_no_motion == 0)
        {
            ms_since_off_no_motion = millis();
        }
        if (ms_since_off_no_motion > (millis() + (5U * 60U * 1000U)))
        {
            mqttLogger.println("been a 5 minutes with no motion CanSleep NOW??");
        }
        no_motion_debounce = 0;
        motion_Calibrate_debounce = 0;
    }
    if ((motion || simulatedMotionTrigger) == true)
    {
        ms_since_off_no_motion = 0;
        digitalWrite(CAM_PIN, HIGH);
        Serial.println("Motion detected ");
        fast_led::set_fast_led(0, CRGB::Blue);
        delay(50);
        if (motion_Calibrate_debounce++ > 200)
        {
            fast_led::set_fast_led(0, CRGB::Green);
            motion_Calibrate_debounce = 0;
            imu_dmp::resetBaseline();
            Serial.println("Motion calibration too much movement");
            delay(250);
        }
        no_motion_debounce = 0;
        fast_led::set_fast_led(0, CRGB(0, 0, 6));
        delay(50);
    }
    if (!(motion || simulatedMotionTrigger))
    {
        no_motion_debounce = 0;
        motion_Calibrate_debounce = 0;
    }
}

void loopWifiStatus()
{
    if (power::iskeyShortPressed() || simulatedPowerKeyTrigger)
    {
        simulatedPowerKeyTrigger = false;
        mqttLogger.println("Power key short pressed detected in main loop");
        if (!GetWifiOn())
        {
            LastWifiOnTimestamp = millis();
            StartWifi();
        }
        else
        {
            // If WiFi task is already running, check if we need to stop it
            if ((millis() - LastWifiOnTimestamp) > (1 * 60 * 1000)) // 10 minutes
            {
                mqttLogger.println("Stopping WiFi OTA task due to timeout");
                StopWifi();
            }
        }
    }
}

void loop()
{
    loopWifiStatus();
    loopPowerCheck();
    motion = imu_dmp::imu_get_moved();
    if (motion)
    {
        mqttLogger.println("MPU loop Motion detected");
    }
    if (!power::isPowerVBUSOn())
    {
        loopImuMotion();
    }
    delay(1000);
}

