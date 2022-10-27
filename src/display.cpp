#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

Adafruit_SH1107 display = Adafruit_SH1107(64, 128, &Wire);

// OLED FeatherWing buttons map to different pins depending on board:
#if defined(ESP8266)
  #define BUTTON_A  0
  #define BUTTON_B 16
  #define BUTTON_C  2
#elif defined(ESP32) && !defined(ARDUINO_ADAFRUIT_FEATHER_ESP32S2)
  #define BUTTON_A 15
  #define BUTTON_B 32
  #define BUTTON_C 14
#elif defined(ARDUINO_STM32_FEATHER)
  #define BUTTON_A PA15
  #define BUTTON_B PC7
  #define BUTTON_C PC5
#elif defined(TEENSYDUINO)
  #define BUTTON_A  4
  #define BUTTON_B  3
  #define BUTTON_C  8
#elif defined(ARDUINO_NRF52832_FEATHER)
  #define BUTTON_A 31
  #define BUTTON_B 30
  #define BUTTON_C 27
#else // 32u4, M0, M4, nrf52840, esp32-s2 and 328p
  #define BUTTON_A  9
  #define BUTTON_B  6
  #define BUTTON_C  5
#endif

void display_setup()
{
  delay(250); // wait for the OLED to power up
  display.begin(0x3C, true); // Address 0x3C default

  Serial.println("OLED begun");

  display.setRotation(1);
  display.setTextSize(2);
  display.setTextColor(SH110X_WHITE);
}

void display_song(const char* name, const char* status)
{
  static char previous_name[128] = {};
  static char previous_status[128] = {};

  if (!strcmp(previous_name, name) && !strcmp(previous_status, status)) {
    // Do nothing if there are no changes.
    return;
  }

  strcpy(previous_name, name);
  strcpy(previous_status, status);

  Serial.printf("Displaying \"%s\", \"%s\"\r\n", name, status);

  display.clearDisplay();

  display.setCursor(1, 0);
  display.print(name);

  display.setCursor(1, 32);
  display.println(status);

  display.display();
}
