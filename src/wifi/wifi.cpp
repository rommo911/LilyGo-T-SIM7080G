#include "wifi/wifi.hpp"
#include "ArduinoOTA.h"
#include "web_server/web.hpp"

extern const char *ssid;
extern const char *wifiPassword;
extern const char *mqtt_server;
extern const char *mqttUser;
extern const char *mqttPass;
extern const char *cmdTopic;
extern const char *mqttTopic ;

extern uint32_t mqtt_port;
uint32_t last_ota_time = 0;
static String mqttReceStr;

WiFiClient espClient;
PubSubClient mqttclient(espClient);

MqttLogger mqttLogger(mqttclient, mqttTopic, MqttLoggerMode::MqttAndSerial);


void MqttReceiveCallback(char *topic, byte *payload, unsigned int length)
{
  // Safely convert payload (which may not be null-terminated) into a String.
  const unsigned int MAX_PAYLOAD = 1024; // prevent excessive allocation
  unsigned int len = length;
  if (len > MAX_PAYLOAD) {
    len = MAX_PAYLOAD;
    mqttLogger.println("Warning: payload truncated due to size");
  }

  char *buf = (char *)malloc(len + 1);
  if (buf == NULL) {
    mqttLogger.println("Error: malloc failed in MqttReceiveCallback");
    return;
  }

  if (len > 0) {
    memcpy(buf, payload, len);
  }
  buf[len] = '\0'; // ensure null termination

  mqttReceStr = String(buf);
  free(buf);

  Serial.printf("got message on topic %s = %s \n", topic, mqttReceStr.c_str());
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
        type = "firmware.......";
      } else {  // U_SPIFFS
        type = "filesystem.......";
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
  mqttclient.setServer(mqtt_server, mqtt_port);
  fs::fs_server_setup();
  while (1)
  {
    while (!mqttclient.connected())
    {
      mqttLogger.println("Attempting MQTT connection...\n");
      // Attempt to connect
      if (mqttclient.connect("ESP32Tsim7080Logger", mqttUser, mqttPass))
      {
        // as we have a connection here, this will be the first message published to the mqtt server
        mqttLogger.println("connected.");
        mqttclient.subscribe(cmdTopic, 1);
      }
      else
      {
        mqttLogger.printf("failed, rc=%d \n", mqttclient.state());
        // Wait 5 seconds before retrying
        delay(5000);
      }
    }
    mqttclient.loop();
    ArduinoOTA.handle();
  }
}


void setUpWifiAP()
{
  WiFi.mode(WIFI_AP);
  uint32_t counter = 0;
  Serial.print("creating wifi .");
  fs::fs_server_setup();
}