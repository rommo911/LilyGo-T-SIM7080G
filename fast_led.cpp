#include "fast_led.hpp"
#include <mutex>
#include <condition_variable>

namespace fast_led
{

    std::mutex fast_led_mtx;
    std::condition_variable cv;

    CRGB leds[2] = {CRGB::Black, CRGB::Black};
    CRGB last_leds[2] = {CRGB::Black, CRGB::Black};

    TaskHandle_t blinkTask;
    uint16_t delay_blink = 0;
    uint16_t blink_counter = 0;
    std::atomic<bool> blinking = false;

    void blink_loop(void *arg)
    {
        while (1)
        {
            while (blinking && blink_counter > 0)
            {
                blink_counter--;
                last_leds[0] = leds[0];
                last_leds[1] = leds[1];
                set_fast_led(0, CRGB::Black);
                set_fast_led(1, CRGB::Black);
                delay(delay_blink);
                set_fast_led(0, last_leds[0]);
                set_fast_led(1, last_leds[1]);
            }
            blink_counter = 0;
            blinking = false;
            delay(50);
        }
    }

    void fast_led_init()
    {
        FastLED.addLeds<WS2811, ws8128_PIN, RGB>(leds, 2);
        xTaskCreate(loop_fast_led, "loop_fast_led", 4096, NULL, 1, NULL);
        xTaskCreate(blink_loop, "blink_loop", 4096, NULL, 1, &blinkTask);
    }

    void set_blink(bool enable, uint16_t ms, uint16_t count)
    {
        if (enable)
        {
            blink_counter = count > 0 ? count : 33333;
            delay_blink = ms > 0 && ms < 1000 ? ms : 1000;
            blinking = true;
        }
        else
        {
            blinking = false;
            blink_counter = 0;
        }
    }

    void loop_fast_led(void *arg)
    {
        while (1)
        {
            {
                std::unique_lock lk(fast_led_mtx);
                cv.wait(lk);
                FastLED.show();
            }
            delay(20);
        }
        vTaskDelete(NULL);
    }

    void set_fast_led(uint8_t index, CRGB color)
    {
        std::lock_guard<std::mutex> lock(fast_led_mtx);
        leds[index] = color;
        cv.notify_one();
    }

}