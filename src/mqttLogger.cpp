#include <WiFi.h>
#include <PubSubClient.h>
#include <MqttLogger.h>

void loopMqtt(void *arg);

extern const char *mqttTopic;
extern const char *cmdTopic;
extern const char * mqtt_server;
extern uint32_t mqtt_port ;

WiFiClient espClient;
PubSubClient mqttclient(espClient);

// default mode is MqttLoggerMode::MqttAndSerialFallback
MqttLogger mqttLogger(mqttclient, mqttTopic, MqttLoggerMode::MqttOnly);
// other available modes:
// MqttLogger mqttLogger(mqttclient,"mqttlogger/log",MqttLoggerMode::MqttAndSerial);

// arduino setup
void setupMqttLogger()
{
    Serial.begin(115200);
    // MqttLogger uses mqtt when available and Serial as a fallback. Before any connection is established,
    // MqttLogger works just like Serial
    mqttLogger.println("Starting setup..");
    mqttLogger.setBufferSize(512);
    mqttclient.setServer(mqtt_server, mqtt_port);

    xTaskCreate(loopMqtt, "mqttLogger", 4096, NULL, 1, NULL);
}

void loopMqtt(void *arg)
{
    while (1)
    {
        while (!mqttclient.connected())
        {
            mqttLogger.print("Attempting MQTT connection...\n");
            // Attempt to connect
            if (mqttclient.connect("ESP32Tsim7080Logger", "rami", "Rr0033141500!"))
            {
                // as we have a connection here, this will be the first message published to the mqtt server
                mqttLogger.println("connected.");

                mqttclient.subscribe(cmdTopic, 1);
            }
            else
            {
                mqttLogger.print("failed, rc=");
                mqttLogger.print(mqttclient.state());
                mqttLogger.println(" try again in 5 seconds");
                // Wait 5 seconds before retrying
                delay(5000);
            }
        }
        mqttclient.loop();
        delay(10);
    }
    // with a connection, subsequent print() publish on the mqtt broker, but in a buffered fashion
}
