#include "MqttLogger.h"
#include "Arduino.h"

MqttLogger::MqttLogger(PubSubClient &client, const char *topic, MqttLoggerMode mode, const boolean &retained)
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
    if (this->client == nullptr)
    {
        return 0;
    }
    char loc_buf[128];
    char *temp = loc_buf;
    va_list arg;
    va_list copy;
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
    { // comparation of same sign type for the compiler
        temp = (char *)malloc(len + 50);
        if (temp == NULL)
        {
            va_end(arg);
            return 0;
        }
        len = vsnprintf(temp, len + 50, format, arg);
    }
    va_end(arg);
    bool ret = true;
    if (this->mode != MqttLoggerMode::SerialOnly && client != nullptr)
    {
        if (this->client->connected() == true)
        {
            ret = this->client->publish(this->topic, temp, retained);
        }
    }
    if (this->mode != MqttLoggerMode::MqttOnly)
    {
        Serial.println((char *)temp);
    }
    if (temp != loc_buf)
    {
        free(temp);
    }
    return ret ? (size_t)len : -1;
}

size_t MqttLogger::printf(const char *_topic, const char *format, ...)
{
    char loc_buf[256];
    char *temp = loc_buf;
    va_list arg;
    va_list copy;
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
    { // comparation of same sign type for the compiler
        temp = (char *)malloc(len + 50);
        if (temp == NULL)
        {
            va_end(arg);
            return 0;
        }
        len = vsnprintf(temp, len + 50, format, arg);
    }
    va_end(arg);
    bool ret = true;
    if (this->mode != MqttLoggerMode::SerialOnly && client != nullptr)
    {
        if (this->client->connected() == true)
        {
            ret = this->client->publish(_topic, temp, retained);
        }
    }
    if (this->mode != MqttLoggerMode::MqttOnly)
    {
        Serial.printf("%s : %s\n\r", _topic, (char *)temp);
    }
    if (temp != loc_buf)
    {
        free(temp);
    }
    return ret ? (size_t)len : -1;
}

size_t MqttLogger::println(const char *_topic, const char *s)
{
    if (s == nullptr)
        return 0;
    bool ret = true;
    if (this->mode != MqttLoggerMode::SerialOnly && client != nullptr)
    {
        if (this->client->connected() == true)
        {
            ret = this->client->publish(_topic, s, retained);
        }
    }
    if (this->mode != MqttLoggerMode::MqttOnly)
    {
        Serial.printf("topic %s: %s\n", _topic, s);
    }
    return ret ? (size_t)strlen(s) : -1;
}

size_t MqttLogger::println(const char *s)
{
    if (s == nullptr)
        return 0;
    bool ret = true;
    if (this->mode != MqttLoggerMode::SerialOnly && client != nullptr)
    {
        if (this->client->connected() == true)
        {
            ret = this->client->publish(this->topic, s, retained);
        }
    }
    if (this->mode != MqttLoggerMode::MqttOnly)
    {
        Serial.println(s);
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
