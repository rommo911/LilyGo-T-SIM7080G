#include "MqttLogger.h"
#include "Arduino.h"

MqttLogger::MqttLogger(PubSubClient &client, const char *topic, MqttLoggerMode mode, const bool retained)
{
    this->setClient(client);
    this->setTopic(topic);
    this->setMode(mode);
    this->setRetained(retained);
}

MqttLogger::~MqttLogger()
{
}

void MqttLogger::setClient(PubSubClient &client)
{
    this->client = &client;
}

void MqttLogger::setTopic(const char *_topic)
{
    this->topic = _topic;
}

size_t MqttLogger::printf(const char *format, ...)
{
    char loc_buf[256];
    char *temp = loc_buf;
    va_list arg, copy;
    va_start(arg, format);
    va_copy(copy, arg);
    int len = vsnprintf(temp, sizeof(loc_buf), format, copy);
    va_end(copy);

    if (len < 0)
    {
        va_end(arg);
        return 0;
    }

    if (len >= (int)sizeof(loc_buf))
    {
        temp = (char *)malloc(len + 1);
        if (!temp)
        {
            va_end(arg);
            return 0;
        }
        vsnprintf(temp, len + 1, format, arg);
    }
    va_end(arg);

    bool ok = true;
    if (this->mode != MqttLoggerMode::MqttOnly)
        Serial.printf("%s\r\n", temp);

    if (this->mode != MqttLoggerMode::SerialOnly && client && client->connected())
        ok = this->client->publish(this->topic, temp, retained);

    if (temp != loc_buf)
        free(temp);
    return ok ? (size_t)len : 0;
}

size_t MqttLogger::printf(const char *_topic, const char *format, ...)
{
    char loc_buf[256];
    char *temp = loc_buf;
    va_list arg, copy;
    va_start(arg, format);
    va_copy(copy, arg);
    int len = vsnprintf(temp, sizeof(loc_buf), format, copy);
    va_end(copy);

    if (len < 0)
    {
        va_end(arg);
        return 0;
    }

    if (len >= (int)sizeof(loc_buf))
    {
        temp = (char *)malloc(len + 1);
        if (!temp)
        {
            va_end(arg);
            return 0;
        }
        vsnprintf(temp, len + 1, format, arg);
    }
    va_end(arg);

    bool ok = true;
    if (this->mode != MqttLoggerMode::MqttOnly)
        Serial.printf("%s : %s\r\n", _topic, temp);

    if (this->mode != MqttLoggerMode::SerialOnly && client && client->connected())
        ok = this->client->publish(_topic, temp, retained);

    if (temp != loc_buf)
        free(temp);
    return ok ? (size_t)len : 0;
}

size_t MqttLogger::println(const char *_topic, const char *s)
{
    if (s == nullptr)
        return 0;
    bool ret = true;
    if (this->mode != MqttLoggerMode::MqttOnly)
    {
        Serial.printf("topic %s: %s\n", _topic, s);
    }
    if (this->mode != MqttLoggerMode::SerialOnly && client != nullptr)
    {
        if (this->client->connected() == true)
        {
            ret = this->client->publish(_topic, s, retained);
        }
    }
    return ret ? (size_t)strlen(s) : -1;
}

size_t MqttLogger::println(const char *s)
{
    if (s == nullptr)
        return 0;
    bool ret = true;
    if (this->mode != MqttLoggerMode::MqttOnly)
    {
        Serial.println(s);
    }
    if (this->mode != MqttLoggerMode::SerialOnly && client != nullptr)
    {
        if (this->client->connected() == true)
        {
            ret = this->client->publish(this->topic, s, retained);
        }
    }

    return ret ? (size_t)strlen(s) : -1;
}

void MqttLogger::setMode(MqttLoggerMode mode)
{
    this->mode = mode;
}

void MqttLogger::setRetained(const boolean &retained)
{
    this->retained = retained;
}
