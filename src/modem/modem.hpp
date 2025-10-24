
#pragma once

#include <TinyGsmClient.h>
namespace modem
{
    class GPS_INFO
    {
    public:
        float lat;
        float lon;
        float speed;
        float alt;
        int vsat;
        int usat;
        float accuracy;
        int year;
        int month;
        int day;
        int hour;
        int min;
        int sec;
        uint32_t lastupdateTs;
    };

    bool initModem7080();
    bool startModem();
    bool SetGPS(bool enable);
    const GPS_INFO &getGPSInfo();
    bool SetGPRS(bool enable);
    bool shutdownModem();
    bool setRF(bool enable);

    extern TinyGsm modem7080g;
    extern TinyGsmClient client;
    // extern TinyGsmClientSecure secureClient;
    // void loop_ppp();
    // void setup_ppp();

}