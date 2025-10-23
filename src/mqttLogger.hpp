#ifndef MQTTLOGGER_H_
#define MQTTLOGGER_H_

#include <MqttLogger.h>

extern MqttLogger mqttLogger;
extern PubSubClient mqttclient;

void setupMqttLogger();

#endif // MQTTLOGGER_H_