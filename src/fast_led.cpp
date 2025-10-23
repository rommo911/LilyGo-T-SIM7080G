#include "fast_led.hpp"
#include <mutex>
#include <condition_variable>
#include <atomic>

namespace fast_led
{

    #define NUM_LEDS 1

    std::mutex fast_led_mtx;
    std::mutex fast_led_mtx2;
    std::condition_variable cv;

    CRGB leds[NUM_LEDS] = {};
    CRGB last_leds[NUM_LEDS] = {};

    TaskHandle_t blinkTask;
    uint16_t delay_blink = 0;
    uint16_t blink_counter = 0;
    std::atomic<bool> blinking = {false};

    void loop_fast_led(void *arg)
    {
        while (1)
        {
            {
                std::unique_lock<std::mutex> lk(fast_led_mtx2);
                cv.wait(lk);
                FastLED.show();
                //Serial.println("LED updated");
            }
            delay(5);
        }
        vTaskDelete(NULL);
    }

    void blink_loop(void *arg)
    {
        while (1)
        {
            while (blinking && blink_counter > 0)
            {
                //Serial.printf("Blinking... %d\n", blink_counter);
                blink_counter--;
                last_leds[0] = leds[0];
                set_fast_led(0, CRGB::Black);
                delay(delay_blink);
                set_fast_led(0, last_leds[0]);
                delay(delay_blink);
                if (blink_counter == 0)
                {
                    set_fast_led(0, CRGB::Black);
                }
            }
            blink_counter = 0;
            blinking = false;
            delay(50);
        }
    }

    void fast_led_init()
    {
        FastLED.addLeds<WS2811, PIXEL_LED_PIN, GRB>(leds, NUM_LEDS);
        xTaskCreate(blink_loop, "blink_loop", 4096, NULL, 1, &blinkTask);
        xTaskCreate(loop_fast_led, "loop_fast_led", 4096, NULL, 1, NULL);
        set_fast_led(0, CRGB::Blue);
        delay(500);
        set_fast_led(0, CRGB::Black);
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

    void set_fast_led(uint8_t index, CRGB color)
    {
        // std::lock_guard<std::mutex> lock(fast_led_mtx);
        leds[0] = color;
        cv.notify_all();
    }

}