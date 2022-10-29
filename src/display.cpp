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

void write_display(const char*, const char*);

void display_setup()
{
  delay(250); // wait for the OLED to power up
  display.begin(0x3C, true); // Address 0x3C default

  Serial.println("OLED begun");

  display.setRotation(1);
  display.setTextSize(2);
  display.setTextColor(SH110X_WHITE);
}

void display_text(const char* top, const char* bottom)
{
  const int scroll_frames = 60;
  const int top_length = 30;
  const int bottom_length = 10;
  static const char* previous_top = NULL;
  static const char* previous_bottom = NULL;
  static int scroll_frame = 0;
  char top_buf[top_length + 1] = {};
  char bottom_buf[bottom_length + 1] = {};

  // First run pretend it's the buffers to avoid comparison to NULL.
  if (previous_top == NULL) previous_top = top_buf;
  if (previous_bottom == NULL) previous_bottom = bottom_buf;

  bool new_text = false;
  bool top_scroll = strlen(top) > top_length;
  bool bottom_scroll = strlen(bottom) > bottom_length;
  bool scrolling = top_scroll || bottom_scroll;
  bool top_changed = strcmp(previous_top, top);
  bool bottom_changed = strcmp(previous_bottom, bottom);

  // Do nothing if there are no changes and no scrolling to perform.
  if (!scrolling && !top_changed && !bottom_changed) {
    return;
  } else if (top_changed || bottom_changed) {
    // Reset scrolling - input changed. Ensure a refresh happens, as the text
    // could change within the first scroll frame.
    scroll_frame = 0;
    new_text = true;

    previous_top = top;
    previous_bottom = bottom;
  }

  // Update scroll frame, and determine if the scroll position has changed.
  // Update anyway if there's new text to show.
  int previous_scroll_offset = scroll_frame / scroll_frames;
  scroll_frame++;
  int scroll_offset = scroll_frame / scroll_frames;
  if (!new_text && previous_scroll_offset == scroll_offset) {
    return;
  }

  // Start at scroll offset; cap length to displayable
  int top_offset = top_scroll ? scroll_offset : 0;
  int bottom_offset = bottom_scroll ? scroll_offset : 0;

  strncpy(top_buf, top + top_offset, top_length);
  strncpy(bottom_buf, bottom + bottom_offset, bottom_length);

  // If scrolling has finished going through the entire line, reset scrolling.
  if (strlen(top_buf) < top_length || strlen(bottom_buf) < bottom_length) {
    scroll_frame = 1;
    strncpy(top_buf, top, top_length);
    strncpy(bottom_buf, bottom, bottom_length);
  }

  write_display(top_buf, bottom_buf);
}

void write_display(const char *top, const char *bottom) {
  Serial.printf("Displaying \"%s\", \"%s\"\r\n",
                top, bottom);

  display.clearDisplay();

  display.setCursor(0, 0);
  display.print(top);

  display.setCursor(0, 50);
  display.println(bottom);

  display.display();
}
