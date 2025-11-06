#ifndef MAIN_H_
#define MAIN_H_
#include <Arduino.h>

void turnOnCamera();
void turnOffCamera();
bool getCamIsON();

uint64_t getLastCamOnTs();

uint64_t getLastCamOffTs();

void loadTimingPref();
#endif // MAIN_H_