#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

const int display_width = 128;
const int display_height = 64;

Adafruit_SH1107 display = Adafruit_SH1107(display_height, display_width, &Wire);

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

  return true;
}

void display_text(const char* top, const char* bottom)
{
  const int previous_line_str_len = 128;
  const int scroll_frames = 10;
  const int character_width = 7;
  const int characters_per_line = display_width / character_width / 2;

  // Max number of characters that will fit on the screen
  const int max_top_characters = characters_per_line * 3;
  const int max_bottom_characters = characters_per_line;

  static char previous_top[previous_line_str_len + 1] = {};
  static char previous_bottom[previous_line_str_len + 1] = {};
  static int top_scroll_frame = 0;
  static int bottom_scroll_frame = 0;

  bool top_scroll = strlen(top) > max_top_characters;
  bool bottom_scroll = strlen(bottom) > max_bottom_characters;
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
  // TODO: Why *2? What is that doing? Without it, every other frame the last character getting a pixel closer to the left is the only change.
  int top_offset = top_scroll ? top_scroll_offset * 2 : 0;
  int bottom_offset = bottom_scroll ? bottom_scroll_offset * 2 : 0;

  // TODO: Hold for a bit at the start of the string instead of immediately scrolling it away.
  // TODO: Hold for a bit when showing end of string instead of just continuing to scroll.
  // TODO: Why am I having to add/subtract offsets specific to each string to get it to continue scrolling the desired amount?
  //       Only 2 additional character for Title Screen; 8 for Reach For Summit.
  int top_characters_offset = min(top_offset / character_width, strlen(top));
  int bottom_characters_offset = min(bottom_offset / character_width, strlen(top));
  int top_max_offset = strlen(top + top_characters_offset) * character_width;
  int bottom_max_offset = strlen(bottom + bottom_characters_offset) * character_width;

  // If scrolling has finished going through the entire line, reset scrolling.
  if (top_offset >= top_max_offset) {
    top_scroll_frame = 1;
  }

  if (bottom_offset >= bottom_max_offset) {
    bottom_scroll_frame = 1;
  }

  // TODO: Truncate to what actually fits on the display: top_length / bottom_length
  Serial.printf("Displaying \"%s\" (%03d), \"%s\" (%03d)\r\n",
                top + min(top_offset / character_width, strlen(top)), top_offset,
                bottom + (bottom_offset / character_width), bottom_offset);

  display.clearDisplay();

  display.setCursor(-top_offset, 0);
  display.print(top);

  display.fillRect(0, 48, 128, 16, SH110X_BLACK);

  display.setCursor(-bottom_offset, 48);
  display.println(bottom);

  display.display();
}
