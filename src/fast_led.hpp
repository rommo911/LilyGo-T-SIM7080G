#include <Arduino.h>
#include <FastLED.h>
#include "pins.hpp"

namespace fast_led
{
    void set_blink(bool enable, uint16_t ms = 100, uint16_t count = 33333);

    void fast_led_init();

    void set_fast_led(uint8_t index, CRGB  color);
}
