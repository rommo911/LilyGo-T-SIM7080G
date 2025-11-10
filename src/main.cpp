
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
#include "imu6500/imu_DMP6.hpp"
#include "wifi/wifi.hpp"
#include "modem/modem.hpp"
#include "Preferences.h"
#include <thread>

bool simulatedMotionTrigger = false;
bool simulatedLowPowerTrigger = false;
bool simulatedCriticalLowPowerTrigger = false;

static uint64_t LastWifiOnTimestamp = 0;

static uint32_t RTC_DATA_ATTR motionCounter;

static imu6500_dmp::MotionDtect_t motionInfo = {};

power::WakeUpReason wu;

static inline bool NoMotionSince(const uint32_t timeout)
{
    return (millis() - imu6500_dmp::getLastMovedTimestamp() > timeout);
}

static inline bool NoVbusSince(const uint32_t timeout)
{
    return (millis() - power::getLastVbusRemovedTs() > timeout);
}

void CheckMotionCount()
{
    if (motionCounter > 0)
    {
        motionCounter = 0;
        fast_led::start_blink(1, {50, 50, 50}, CRGB::Red, 250, 150, 5000);
        delay(5000);
    }
}

void setup()
{
    uint8_t counter = 0;
    Serial.begin(115200);
    wu = power::Get_wake_reason();

    power::setupPower();
    bool ret = false;
    // setCpuFrequencyMhz(80);
    WiFi.mode(WIFI_OFF);
    // Serial.setTxBufferSize(512);
    fast_led::fast_led_init();
    fast_led::set_solid(0, {0, 0, 30});
    pinMode(CAM_PIN, OUTPUT);
    turnOnCamera();
    loadTimingPref();
    if (imu6500_dmp::imu_setup(imu6500_dmp::WOM))
    {
        Serial.println("IMU setup complete ");
    }
    else
    {
        Serial.println("IMU setup failed ");
        fast_led::start_blink(0, CRGB::Red);
        delay(5000);
        fast_led::stop_led(0);
        delay(50);
        ESP.restart();
    }
    if (power::isPowerVBUSOn())
    {
        StartWifi();
        LastWifiOnTimestamp = millis();
        counter = 0;
        while (!Serial && counter++ < 20)
        {
            delay(100);
        };
        delay(1500);
        mqttLogger.println("wake with VBUS ON");
    }
    else
    {
        switch (wu)
        {
        case power::WakeUpReason::UNKNOWN:
        {
            motionCounter = 0;
            break;
        }
        case power::WakeUpReason::START:
        {
            break;
        }
        case power::WakeUpReason::MOTION:
        {
            mqttLogger.println("wake FROM MOTION");
            fast_led::start_blink(0, {0, 0, 100}, CRGB::Black, 200, 200, 2000);
            delay(1000);
            if (power::isBatLowLevel())
            {
                fast_led::set_solid(0, {0, 5, 0});
                fast_led::set_solid(1, {5, 0, 0});
            }
            else
            {
                fast_led::set_solid(0, {0, 20, 0});
                fast_led::set_solid(1, {20, 0, 0});
            }
            if (!power::isPowerVBUSOn())
            {
                if (power::isBatLowLevel())
                {
                    power::DeepSleepWith_Timer_Wake(getNoMotionTimeout()); // dont keep waking up for new motion !
                }
                else
                {
                    power::DeepSleepWith_IMU_Timer_Wake(getNoMotionTimeout()); // wake up and reset timer if new motion is detected before expires
                }
            }

            break;
        }
        case power::WakeUpReason::TIMER:
        {
            fast_led::start_blink(0, {50, 50, 50}, CRGB::Black, 200, 200, 1000);
            delay(1000);
            if (!imu6500_dmp::getMotion() && !power::isPowerVBUSOn())
            {
                mqttLogger.println("wake FROM Timer .. turn off Cam");
                motionCounter++;
                turnOffCamera();
                if (power::isBatLowLevel())
                    fast_led::set_solid(0, {2, 0, 0}); // turn off led before sleep
                else
                    fast_led::set_solid(0, {0, 0, 3}); // turn off led before sleep

                fast_led::stop_led(1); // turn off led before sleep
                power::DeepSleepWith_IMU_PMU_Wake();
            }

            break;
        }
        default:
        {
            mqttLogger.println("wake FROM unknwon");
            break;
        }
        }
    }
    if (power::isPowerVBUSOn())
    {
        if (sdcard::checkForupdatefromSD())
        {
            Serial.println("update done restarting");
            delay(100);
            ESP.restart();
        }
    }
}

void loopPowerCheck()
{
    if (power::isPowerVBUSOn())
    {
        const uint8_t percent = power::getPMU().getBatteryPercent();
        fast_led::set_solid(1, batteryColor(percent));
        CheckMotionCount();
        return;
    }
    if (power::isBatCriticalLevel())
    {
        mqttLogger.printf("Battery critical level detected in main loop \n");
        fast_led::start_blink(0, {20, 0, 0}, CRGB::Black, 75, 125, 500);
        delay(500);
        fast_led::stop_led(1);
        fast_led::stop_led(0);
        power::getPMU().shutdown();
    }
    if (power::isBatLowLevel() || simulatedLowPowerTrigger)
    {
        simulatedLowPowerTrigger = false;
        mqttLogger.printf("Battery low level detected in main loop \n");
        fast_led::set_solid(0, {2, 0, 0}); // turn off led before sleep
        fast_led::stop_led(1);             // turn off led before sleep
        delay(30);
        power::DeepSleepWith_IMU_PMU_Wake();
    }
}

bool waitForCarhelper = false;
void loopImuMotion()
{
    if (imu6500_dmp::getMotion())
    {
        fast_led::start_blink(0, {0, 0, 100}, CRGB::Black, 150, 200, 200); // turn off led before sleep
        delay(150);
    }
    if (power::isPowerVBUSOn())
    {
        waitForCarhelper = true;
        return;
    }
    // wait untill no motion for a while and Vbus removed for a while
    if (NoMotionSince(getNoMotionTimeout()) && NoVbusSince(getSecureModeTimeout()))
    {
        mqttLogger.println("starting secure mode");
        turnOffCamera();
        fast_led::set_solid(0, {0, 0, 2}); // turn off led before sleep
        fast_led::stop_led(1);             // turn off led before sleep            modem::shutdownModem();
        power::DeepSleepWith_IMU_PMU_Wake();
    }
    else
    {
        if (waitForCarhelper) // only once
        {
            fast_led::start_blink(1, batteryColor(power::getPMU().getBatteryPercent()), CRGB::Black, 100, 2500, 120 * 1000); // crete blink patter to inform user its waiting
            waitForCarhelper = false;
            mqttLogger.println("waiting for some time after vbus removed");
        }
    }
    return;
}

void loopWifiStatus()
{
    if (power::iskeyShortPressed())
    {
        mqttLogger.println("Power key short pressed detected in main loop");
        if (!GetWifiOn())
        {
            fast_led::start_blink(1, {0, 50, 50}, CRGB::Black, 200, 2500, 120 * 1000); // crete blink patter to inform user its waiting
            StartWifi();
            LastWifiOnTimestamp = millis();
        }
    }
    if (GetWifiOn())
    {
        if (((millis() - LastWifiOnTimestamp > getWifiTimeout()) || (!power::isPowerVBUSOn() && power::isBatLowLevel())))
        {
            mqttLogger.println("WiFi on timeout reached, turning off WiFi");
            StopWifi();
        }
    }
}

void loop()
{
    loopWifiStatus();
    loopPowerCheck();
    loopImuMotion();
    delay(10);
}
