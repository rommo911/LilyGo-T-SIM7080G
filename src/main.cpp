
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

bool simulatedMotionTrigger = false;
bool simulatedLowPowerTrigger = false;
bool simulatedCriticalLowPowerTrigger = false;

static uint64_t LastWifiOnTimestamp = 0;

static uint32_t WifiTimeout = 1000U * 60U * 5U;  //
static uint32_t No_MotionTimeout = 10U * 1000U;  //
static uint32_t SecureModeTimeout = 10U * 1000U; //

static bool SecureMode = false;

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

void loadTimingPref()
{
    Preferences pref;
    pref.begin("timing", true);
    WifiTimeout = pref.getInt("wifitm", WifiTimeout);                  //
    No_MotionTimeout = pref.getInt("nomotiontm", No_MotionTimeout);    //
    SecureModeTimeout = pref.getInt("securmodetm", SecureModeTimeout); //
    mqttLogger.printf(" timing wifitm %d , nomotiontm %d , securmodetm %d \n", WifiTimeout, No_MotionTimeout, SecureModeTimeout);
}

void setup()
{
    bool ret = false;
    Serial.begin(115200);
    Serial.setTxBufferSize(512);
    uint8_t counter = 0;
    power::setupPower();
    fast_led::fast_led_init();
    pinMode(CAM_PIN, OUTPUT);
    turnOnCamera();
    if (power::isPowerVBUSOn())
    {
        StartWifi();
        while (!Serial && counter++ < 20)
        {
            delay(200);
        };
    }
    loadTimingPref();
    modem::shutdownModem();
    // delay(2000);
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
        delay(50);
        ESP.restart();
    }
    setCpuFrequencyMhz(80);

    /*bool modRet = modem::initModem7080();
    if (modRet)
    {
        modem::setRF(false);
        modem::SetGPS(true);
    }*/
    fast_led::set_solid(0, {0, 0, 50});
    wu = power::Get_wake_reason();
    switch (wu)
    {
    case power::WakeUpReason::MOTION:
    {
        mqttLogger.println("wake FROM MOTION");
        fast_led::start_blink(0, CRGB::Blue, CRGB::Black, 200, 200, 2);
        break;
    }
    case power::WakeUpReason::START:
    {
        fast_led::start_blink(1, CRGB::Green, CRGB::Black, 200, 200, 2);
        mqttLogger.println("wake FROM PMU");
        break;
    }

    default:
    {
        fast_led::start_blink(1, {50, 50, 0}, CRGB::Black, 200, 200, 2);
        mqttLogger.println("wake FROM unknwon");
        break;
    }
    }
    delay(2000);
    if (!power::isPowerVBUSOn())
    {
        uint32_t now = millis();
        while (imu6500_dmp::imu_get_moved() && ((millis() - now) > 30000U))
        {
            fast_led::set_solid(0, {50, 50, 50});
            delay(25);
        }
        fast_led::stop_led(0);
    }
}

void loopPowerCheck()
{
    if (power::isPowerVBUSOn())
    {
        const uint8_t percent = power::getPMU().getBatteryPercent();
        fast_led::set_solid(0, batteryColor(percent));
        if (!simulatedLowPowerTrigger && !simulatedCriticalLowPowerTrigger)
            return;
    }
    if (power::isBatCriticalLevel() || simulatedCriticalLowPowerTrigger)
    {
        mqttLogger.printf("Battery critical level detected in main loop %d - %d \n", power::isBatCriticalLevel() ? 1 : 0, simulatedCriticalLowPowerTrigger ? 1 : 0);
        simulatedCriticalLowPowerTrigger = false;
        modem::shutdownModem();
        sdcard::shutdownSdcard();
        imu6500_dmp::shutdown();
        power::DeepSleepWith_PMU_Wake();
    }
    if (power::isBatLowLevel() || simulatedLowPowerTrigger)
    {
        simulatedLowPowerTrigger = false;
        mqttLogger.printf("Battery low level detected in main loop %d - %d \n", power::isBatLowLevel() ? 1 : 0, simulatedLowPowerTrigger ? 1 : 0);
        imu6500_dmp::shutdown();
        modem::shutdownModem();
        sdcard::shutdownSdcard();
        imu6500_dmp::imu_WakeOnMotion_LowPwer_setup();
        power::DeepSleepWith_IMU_PMU_Wake();
    }
}

bool waitForLeaveCar = true;
bool waitForCarhelper = true;

void loopImuMotion()
{
    motionInfo = imu6500_dmp::imu_get_moved();
    bool _motionTriggered = motionInfo || simulatedMotionTrigger;
    simulatedMotionTrigger = false;
    if (power::isPowerVBUSOn())
    {
        if (motionCounter > 0)
        {
            fast_led::start_blink(0, CRGB::Blue, CRGB::Black, 150, 150, 5);
            delay(500);
            motionCounter = 0;
        }
        waitForLeaveCar = true;
        waitForCarhelper = true;
        return;
    }
    if (waitForLeaveCar == true) // wait for timeout after vbus inserted (SecureModeTimeout)
    {
        // wait untill no motion for a while and Vbus removed for a while
        if (NoMotionSince(No_MotionTimeout) && NoVbusSince(SecureModeTimeout))
        {
            turnOffCamera();
            fast_led::stop_led(0);
            waitForLeaveCar = false; // exit this mode and start watching out for motion
            mqttLogger.println("startin secure mode");
        }
        else
        {
            if (waitForCarhelper) // only once
            {
                const uint8_t percent = power::getPMU().getBatteryPercent();
                CRGB rgb = batteryColor(percent);
                fast_led::start_blink(0, rgb, CRGB::Black, 350, 2500, 120); // crete blink patter to inform user its waiting
                waitForCarhelper = false;
                mqttLogger.println("waiting for some time after vbus removed");
            }
        }
        return;
    }

    if (_motionTriggered)
    {
        mqttLogger.println("carlog/isntantMotion", "Loop Motion detected ");

        if (getCamIsON() == false)
        {
            mqttLogger.println("carlog/motion", "started cam ");
            turnOnCamera();
            fast_led::start_blink(0, {0, 15, 0}, {50, 0, 0}, 1250, 100);
        }
        if (motionInfo.motionInterrupt)
        {
            if (imu6500_dmp::getbaseline().ready)
            {
                fast_led::start_blink(1, {0, 0, 100}, CRGB::Black, 40, 1500, 1);
            }
            else
            {
                fast_led::start_blink(1, {30, 30, 30}, CRGB::Black, 40, 1500, 1);
            }
        }
        else if (motionInfo.yaw || motionInfo.pitch || motionInfo.roll)
        {
            fast_led::start_blink(1, {100, 0, 0}, CRGB::Black, 40, 1500, 1);
        }
        else
        {
            fast_led::start_blink(1, {0, 50, 50}, CRGB::Black, 40, 1500, 1);
        }
        delay(50);
    }
    else
    {
        if (NoMotionSince(No_MotionTimeout)) // no motion for 5s and car not started
        {
            if (getCamIsON())
            {
                mqttLogger.println("carlog/motion", "stopped cam ");
                turnOffCamera();
                motionCounter++;
                fast_led::start_blink(0, {5, 0, 0}, {0, 0, 0}, 100, 3500);
            }
        }
    }
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
    loopImuMotion();
    delay(10);
}
