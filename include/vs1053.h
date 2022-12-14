#include <stdint.h>
#include <led.h>

bool vs1053_setup();
// Returns whether the display was updated.
bool vs1053_loop();

bool vs1053_changeSong(int direction);
void vs1053_togglePause();

const int no_microsd[] = {short_blink_ms, long_blink_ms, short_blink_ms, 0};

// Feather ESP8266
#if defined(ESP8266)
const uint8_t CARDCS = 2;          // Card chip select pin

// Feather ESP32
#elif defined(ESP32) && !defined(ARDUINO_ADAFRUIT_FEATHER_ESP32S2)
const uint8_t CARDCS = 14;         // Card chip select pin

// Feather Teensy3
#elif defined(TEENSYDUINO)
const uint8_t CARDCS = 8;          // Card chip select pin

// WICED feather
#elif defined(ARDUINO_STM32_FEATHER)
const uint8_t CARDCS = PC5;        // Card chip select pin

#elif defined(ARDUINO_NRF52832_FEATHER)
const uint8_t CARDCS = 27;         // Card chip select pin

// Feather M4, M0, 328, ESP32-S2, nRF52840 or 32u4
#else
const uint8_t CARDCS = 5;          // Card chip select pin

#endif
