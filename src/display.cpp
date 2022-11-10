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

  // Smooth scrolling offsets each line per-pixel.
  display.setTextWrap(false);

  return true;
}

void display_text(const char* top, const char* bottom)
{
  const int previous_line_str_len = 128;
  const int scroll_frames = 10;
  const int character_width = 7;
  const int characters_per_line = display_width / character_width / 2;

  // Max number of characters that will fit completely on the screen when wrapping lines.
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
  int top_offset = top_scroll ? top_scroll_offset : 0;
  int bottom_offset = bottom_scroll ? bottom_scroll_offset : 0;

  const int start_hold = character_width * 2;
  // Hold for a bit at the start of the string instead of immediately scrolling it away.
  if (top_offset < start_hold) {
    top_offset = 0;
  } else {
    top_offset -= start_hold;
  }

  if (bottom_offset < start_hold) {
    bottom_offset = 0;
  } else {
    bottom_offset -= start_hold;
  }

  // If scrolling has finished going through the entire line, reset scrolling.
  if (top_offset >= (int)strlen(top) * character_width) {
    top_scroll_frame = 0;
  }

  if (bottom_offset >= (int)strlen(bottom) * character_width) {
    bottom_scroll_frame = 0;
  }

  // TODO: Truncate to what actually fits on the display: top_length / bottom_length
  Serial.printf("Displaying \"%s\" (%03d/%d), \"%s\" (%03d/%d)\r\n",
                top + min(top_offset / character_width, strlen(top)), top_offset, strlen(top),
                bottom + min(bottom_offset / character_width, strlen(bottom)), bottom_offset, strlen(bottom));

  display.clearDisplay();

  // TODO: There's room here for avoiding the duplicated code with a ScrollableTextArea that takes a number of lines and vertical offset.

  // Allow letters to get cut off by display edges only when scrolling.
  display.setTextWrap(!top_scroll);

  display.setCursor(-top_offset, 0);
  display.print(top);

  // Only need to print subsequent scrolled lines when not wrapping.
  // When wrapping, they're already printed.
  if (top_scroll) {
    display.setCursor(-top_offset - display_width, 16);
    display.print(top);

    display.setCursor(-top_offset - display_width*2, 32);
    display.print(top);
  }

  display.setTextWrap(!bottom_scroll);

  display.setCursor(-bottom_offset, 48);
  display.println(bottom);

  display.display();
}
