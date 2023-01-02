#include <led.h>

#include <vs1053.h>

#include <Arduino.h>

const int short_blink_ms = 100;
const int long_blink_ms = 500;
const int after_pattern_ms = 1500;

void led_init()
{
  // Keep LED on during startup.
  pinMode(LED_BUILTIN, OUTPUT);
  led_on();
}

void led_on()
{
  digitalWrite(LED_BUILTIN, HIGH);
}

void led_off()
{
  digitalWrite(LED_BUILTIN, LOW);
}

void led_blinkCode(const int *delays, uint8_t beep_frequency_code)
{
  // Blink (odd off, even on) until encountering 0 length terminator.
  for (int i = 0; delays[i]; i++) {
    if (i % 2) {
      led_off();
      delay(delays[i]);
    } else {
      led_on();
      vs1053_beep(delays[i], beep_frequency_code);
    }
  }

  led_off();
  delay(after_pattern_ms);
}
