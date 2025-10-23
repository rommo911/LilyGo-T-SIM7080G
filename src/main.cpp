
/**
 * @file      AllFunction.ino
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2022  Shenzhen Xin Yuan Electronic Technology Co., Ltd
 * @date      2022-09-16
 *
 */
#include <Arduino.h>
#include "sdcard.h"
#include "wifi.hpp"
#include "power.hpp"
#include "pins.hpp"
#include "main.hpp"
#include "fast_led.hpp"
#include "imu_DMP6.hpp"
#include "mqttLogger.hpp"




void setup()
{
    bool ret = false;
    fast_led::fast_led_init();
    Serial.begin(115200);

    uint8_t counter = 0;
    while (!Serial && counter++ < 5)
    {
        delay(1000);
    };
    xTaskCreate(setUpWifiOTA, "ota", 4096, NULL, 1, NULL);

    delay(2500);

    Serial.println();

    Serial.println("=========================================");

    power::getWakeupReason();

    if (!psramFound())
    {
        Serial.println("ERROR: PSRAM not found!");
    }

    Serial.println("=========================================");

    power::setupPower();

    Serial.println("=========================================");

    setupSdcard();

    Serial.println("=========================================");

    if (imu_dmp::imu_setup())
    {
        Serial.println("IMU setup complete");
        mqttLogger.println("IMU setup complete ");
    }
    else
    {
        Serial.println("IMU setup failed");
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
}

void loop()
{

    delay(1000);
}
