#include "MqttLogger.h"
#include "Arduino.h"

MqttLogger::MqttLogger(MqttLoggerMode mode)
{
    this->setMode(mode);
    this->setBufferSize(MQTT_MAX_PACKET_SIZE);
}

MqttLogger::MqttLogger(PubSubClient &client, const char *topic, MqttLoggerMode mode, const boolean &retained)
{
    this->setClient(client);
    this->setTopic(topic);
    this->setMode(mode);
    this->setBufferSize(MQTT_MAX_PACKET_SIZE);
    this->setRetained(retained);
}

MqttLogger::~MqttLogger()
{
}

void MqttLogger::setClient(PubSubClient &client)
{
    this->client = &client;
}

void MqttLogger::setTopic(const char *topic)
{
    this->topic = topic;
    logTopic = topic;
    logTopic += "/log";
}

void MqttLogger::setMetricTopic(const char *topic)
{
    uint8_t counter = 0;
    while (this->merticTopic != "" && counter++ < 10)
    {
        delay(10);
    }
    this->merticTopic = topic;
}

size_t MqttLogger::printfTopic(const char *topic, const char *format, ...)
{
    if (this->client->connected() == false)
    {
        return 0;
    }

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
        temp = (char *)malloc(len + 1);
        if (temp == NULL)
        {
            va_end(arg);
            return 0;
        }
        len = vsnprintf(temp, len + 1, format, arg);
    }
    va_end(arg);
    this->setMetricTopic(topic);
    size_t written = this->write((const uint8_t *)temp, (size_t)len);
    if (temp != loc_buf)
    {
        free(temp);
    }
    return written;
}

size_t MqttLogger::printlnTopic(const char *topic, const char *s)
{
    if (this->client->connected() == false)
        return 0;
    if (s == nullptr)
        return this->write('\n');
    this->setMetricTopic(topic);
    size_t n = this->write((const uint8_t *)s, strlen(s));
    n += this->write('\n'); // this triggers sendBuffer() in write(uint8_t)
    return n;
}

void MqttLogger::setMode(MqttLoggerMode mode)
{
    this->mode = mode;
}

void MqttLogger::setRetained(const boolean &retained)
{
    this->retained = retained;
}

uint16_t MqttLogger::getBufferSize()
{
    return this->bufferSize;
}

// allocate or reallocate local buffer, reset end to start of buffer
boolean MqttLogger::setBufferSize(uint16_t size)
{
    if (size == 0)
    {
        return false;
    }
    if (this->bufferSize == 0)
    {
        this->buffer = (uint8_t *)malloc(size);
        this->bufferEnd = this->buffer;
    }
    else
    {
        uint8_t *newBuffer = (uint8_t *)realloc(this->buffer, size);
        if (newBuffer != NULL)
        {
            this->buffer = newBuffer;
            this->bufferEnd = this->buffer;
        }
        else
        {
            return false;
        }
    }
    this->bufferSize = size;
    return (this->buffer != NULL);
}

// send & reset current buffer
void MqttLogger::sendBuffer()
{
    if (this->bufferCnt > 0)
    {
        bool doSerial = this->mode == MqttLoggerMode::SerialOnly || this->mode == MqttLoggerMode::MqttAndSerial;
        if (this->mode != MqttLoggerMode::SerialOnly && this->client != NULL && this->client->connected())
        {
            if (this->merticTopic != "")
            {
                String _topic = merticTopic;
                merticTopic = this->topic;
                merticTopic += "/" + _topic;
                this->client->publish(merticTopic.c_str(), (byte *)this->buffer, this->bufferCnt, retained);
            }
            else
            {
                this->client->publish(this->logTopic.c_str(), (byte *)this->buffer, this->bufferCnt, retained);
            }
        }
        else if (this->mode == MqttLoggerMode::MqttAndSerialFallback)
        {
            doSerial = true;
        }
        if (doSerial)
        {
            Serial.write(this->buffer, this->bufferCnt);
            Serial.println();
        }
        this->bufferCnt = 0;
    }
    this->bufferEnd = this->buffer;
    if (this->merticTopic != "")
    {
        merticTopic = "";
    }
}

// implement Print::write(uint8_t c): store into a buffer until \n or buffer full
size_t MqttLogger::write(uint8_t character)
{
    if (character == '\n') // when newline is printed we send the buffer
    {
        this->sendBuffer();
    }
    else
    {
        if (this->bufferCnt < this->bufferSize) // add char to end of buffer
        {
            *(this->bufferEnd++) = character;
            this->bufferCnt++;
        }
        else // buffer is full, first send&reset buffer and then add char to buffer
        {
            this->sendBuffer();
            *(this->bufferEnd++) = character;
            this->bufferCnt++;
        }
    }
    return 1;
}