#pragma once 
#include "WiFi.h"
#include <MqttLogger.h>

extern MqttLogger mqttLogger;
extern PubSubClient mqttclient;

void setupMqttLogger();

void setUpWifiOTA(void * arg);
void setUpWifiAP();
