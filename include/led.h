#pragma once
#include <stdint.h>

extern const int short_blink_ms;
extern const int long_blink_ms;
extern const int after_pattern_ms;

void led_init();

// Blink the given code, and beep when the LED is on at a default frequency of 375 Hz.
void led_blinkCode(const int *delays, uint8_t beep_frequency_code=0x42);

void led_on();
void led_off();
