#include "pins.hpp"

static bool CamisOn = false;
static uint64_t lastCamOnTs = 0;
static uint64_t lastCamOffTs = 0;
void turnOnCamera()
{
    if (CamisOn == false)
    {
        lastCamOnTs = millis();
        digitalWrite(CAM_PIN, HIGH);
        CamisOn = true;
    }
}
void turnOffCamera()
{
    if (CamisOn == true)
    {
        lastCamOffTs = millis();
        digitalWrite(CAM_PIN, LOW);
        CamisOn = false;
    }
}


bool getCamIsON()
{
    return CamisOn;
}
uint64_t getLastCamOnTs()
{
    return lastCamOnTs;
}

uint64_t getLastCamOffTs()
{
    return lastCamOffTs;
}
