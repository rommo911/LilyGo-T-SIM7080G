
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

void setup()
{
    bool ret = false;
    fast_led::fast_led_init();
    Serial.begin(115200);
    uint8_t counter = 0;
    power::setupPower();
    while (!Serial && counter++ < 5)
    {
        delay(1000);
    };
    power::getWakeupReason();
    modem::shutdownModem();
    delay(500);
    xTaskCreate(setUpWifiOTA, "ota", 4096, NULL, 1, NULL);
    // setUpWifiAP();

    Serial.println("=========================================");

    if (false)//imu_dmp::imu_setup())
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

void loop()
{
    delay(10000000);
}

#ifdef PIO_CI
const char *ssid = WIFI_SSID;
const char *wifiPassword = WIFI_PASS;
const char *mqtt_server = MQTT_SERVER;
const char *mqttTopic = MQTT_TOPIC;
const char *cmdTopic = CMD_TOPIC;
const char *mqttUser = MQTT_USER;
const char *mqttPass = MQTT_PASS;
uint32_t mqtt_port = MQTT_PORT;
#endif