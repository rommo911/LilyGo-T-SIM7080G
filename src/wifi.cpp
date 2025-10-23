#include "wifi.hpp"
#include "WiFi.h"
#include "ArduinoOTA.h"
#include "mqttLogger.hpp"

extern const char *ssid;
extern const char *wifiPassword;
uint32_t last_ota_time = 0;

void MqttReceiveCallback(char *topic, byte *payload, unsigned int length)
{
  String str = String((char *)payload, length);
  Serial.printf("got message on topic %s = %s \n", topic, str.c_str());
  mqttLogger.printfTopic(" got message : %s", str.c_str());
  // handle message arrived
}

void setUpWifiOTA(void *arg)
{
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, wifiPassword);
  uint32_t counter = 0;
  Serial.print("connecting to wifi .");
  while (WiFi.status() != WL_CONNECTED && counter++ < 50)
  {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() != WL_CONNECTED)
  {
    WiFi.disconnect(true);
    delay(100);
    vTaskDelete(NULL);
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  ArduinoOTA
      .onStart([]()
               {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH) {
        type = "sketch";
      } else {  // U_SPIFFS
        type = "filesystem";
      }

      // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
      Serial.println("Start updating " + type); })
      .onEnd([]()
             { Serial.println("\nEnd"); })
      .onProgress([](unsigned int progress, unsigned int total)
                  {
      if (millis() - last_ota_time > 1000) {
        Serial.printf("Progress: %u%%\n", (progress / (total / 100)));
        last_ota_time = millis();
      } })
      .onError([](ota_error_t error)
               {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) {
        Serial.println("Auth Failed");
      } else if (error == OTA_BEGIN_ERROR) {
        Serial.println("Begin Failed");
      } else if (error == OTA_CONNECT_ERROR) {
        Serial.println("Connect Failed");
      } else if (error == OTA_RECEIVE_ERROR) {
        Serial.println("Receive Failed");
      } else if (error == OTA_END_ERROR) {
        Serial.println("End Failed");
      } });
  ArduinoOTA.begin();

  mqttclient.setCallback(MqttReceiveCallback);

  setupMqttLogger();

  while (1)
  {
    delay(5);
    ArduinoOTA.handle();
  }
}
