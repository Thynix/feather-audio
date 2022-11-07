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

bool display_setup()
{
  // Wait for the OLED to power up
  delay(250);

  // Address 0x3C default
  if (!display.begin(0x3C, true)) {
    Serial.println("Display begin() failed");
    return false;
  }

  display.setRotation(1);
  display.setTextSize(2);
  display.setTextColor(SH110X_WHITE);
}

void display_text(const char* top, const char* bottom)
{
  const int previous_line_str_len = 128;
  const int scroll_frames = 60;
  const int top_length = 30;
  const int bottom_length = 10;
  static char previous_top[previous_line_str_len + 1] = {};
  static char previous_bottom[previous_line_str_len + 1] = {};
  static int top_scroll_frame = 0;
  static int bottom_scroll_frame = 0;
  char top_buf[top_length + 1] = {};
  char bottom_buf[bottom_length + 1] = {};

  bool top_scroll = strlen(top) > top_length;
  bool bottom_scroll = strlen(bottom) > bottom_length;
  bool scrolling = top_scroll || bottom_scroll;
  bool top_changed = strcmp(previous_top, top);
  bool bottom_changed = strcmp(previous_bottom, bottom);
  bool text_changed = top_changed || bottom_changed;

  // Do nothing if there are no changes and no scrolling to perform.
  if (!scrolling && !text_changed) {
    return;
  }

  // Reset scrolling - input changed. Ensure a refresh happens, as the text
  // could change within the first scroll frame.
  if (top_changed) {
    top_scroll_frame = 0;
    strncpy(previous_top, top, previous_line_str_len);
  }
  if (bottom_changed) {
    bottom_scroll_frame = 0;
    strncpy(previous_bottom, bottom, previous_line_str_len);
  }

  // Update scroll frames, and determine if the scroll position has changed.
  // Update anyway if there's new text to show.
  int previous_top_scroll_offset = top_scroll_frame / scroll_frames;
  top_scroll_frame++;
  int top_scroll_offset = top_scroll_frame / scroll_frames;

  int previous_bottom_scroll_offset = bottom_scroll_frame / scroll_frames;
  bottom_scroll_frame++;
  int bottom_scroll_offset = bottom_scroll_frame / scroll_frames;

  if (!text_changed &&
      previous_top_scroll_offset == top_scroll_offset &&
      previous_bottom_scroll_offset == bottom_scroll_offset) {
    return;
  }

  // Start at scroll offset; cap length to displayable
  int top_offset = top_scroll ? top_scroll_offset : 0;
  int bottom_offset = bottom_scroll ? bottom_scroll_offset : 0;

  strncpy(top_buf, top + top_offset, top_length);
  strncpy(bottom_buf, bottom + bottom_offset, bottom_length);

  // If scrolling has finished going through the entire line, reset scrolling.
  if (strlen(top_buf) < top_length) {
    top_scroll_frame = 1;
    strncpy(top_buf, top, top_length);
  }

  if (strlen(bottom_buf) < bottom_length) {
    bottom_scroll_frame = 1;
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
