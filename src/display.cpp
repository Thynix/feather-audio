#include <display.h>

#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <SPI.h>
#include <Wire.h>
#include <led.h>

const int no_display[] = {long_blink_ms, short_blink_ms, short_blink_ms, 0};

const int display_width = 128;
const int display_height = 64;

Adafruit_SH1107 display = Adafruit_SH1107(display_height, display_width, &Wire);

const int previous_text_str_len = 512;

class ScrollArea {
private:
  int16_t starting_y;
  int16_t height;
  int scroll_frame_interval;
  // How much lower is a subsequent line below the previous?
  uint8_t y_advance;
  // How much horizontal space within the area can contain text?
  int horizontal_scroll_space;

  char previous_text[previous_text_str_len + 1] = {};
  bool needs_scrolling;
  int scroll_frame;
  uint16_t total_text_width;
  uint16_t font_height;
  const GFXfont *font;
  int font_scale;
  uint16_t line_count;
public:
  // Scroll speed - frames to wait between scroll movements. See target_frametime.
  ScrollArea(int16_t starting_y, uint16_t height, const GFXfont *font, int font_scale, int scroll_frame_interval) {
    this->starting_y = starting_y;
    this->height = height;
    this->font = font;
    this->font_scale = font_scale;
    this->scroll_frame_interval = scroll_frame_interval;

    // Default built-in font is NULL and 6x8.
    y_advance = font ? font->yAdvance : font_scale * 8;
    line_count = max(height / y_advance, 1);
    horizontal_scroll_space = display_width * line_count;
  }

  // Display text, and scroll if necessary on repeated calls.
  // Returns true if the display was updated.
  bool Display(const char* text) {
    bool text_changed = strcmp(previous_text, text);
    if (!text_changed && !needs_scrolling)
      return false;

    display.setFont(font);
    display.setTextSize(font_scale);

    if (text_changed) {
      scroll_frame = 0;
      strncpy(previous_text, text, previous_text_str_len);

      if (strlen(text) > previous_text_str_len) {
        Serial.printf("WARNING - ScrollArea.Display() received text with %d "
                      "characters, but the buffer is only sized for %d.\r\n",
                      strlen(text), previous_text_str_len);
      }

      int16_t x1, y1;
      uint16_t wrap_width, wrap_height;

      // Dimensions with wrap.
      display.setTextWrap(true);
      display.getTextBounds(text, 0, 0, &x1, &y1, &wrap_width, &wrap_height);

      // Dimensions without wrap
      display.setTextWrap(false);
      display.getTextBounds(text, 0, 0, &x1, &y1, &total_text_width, &font_height);

      //Serial.printf("%hu / %d pixels for '%s'", total_text_width, horizontal_scroll_space, text);
      //Serial.println();

      needs_scrolling = total_text_width > horizontal_scroll_space;
    } else {
      // No text change, but may need to scroll if the frame advanced past the interval.
      scroll_frame++;
      if ((scroll_frame - 1) / scroll_frame_interval == scroll_frame / scroll_frame_interval)
        return false;
    }

    // Enable text wrap only when not scrolling. Done after scrolling determination so it's correct on the first update.
    display.setTextWrap(!needs_scrolling);

    // Before updating, clear previously displayed text within the area.
    display.fillRect(0, starting_y, display_width, height, SH110X_BLACK);

    int offset = scroll_frame / scroll_frame_interval;

    // Hold for this many frame intervals at the start/end of the string instead of immediately continuing.
    const int ends_hold = 14;
    // How much offset is needed for the end of the text to be displayed
    uint16_t text_overflow = total_text_width - horizontal_scroll_space + 1;
    if (offset <= ends_hold) {
      // Hold at start
      offset = 0;
    } else if (offset <= text_overflow + ends_hold) {
      // Compensate for holding at start
      offset -= ends_hold;
    } else {
      // Reset scrolling after end hold
      if (offset > text_overflow + 2*ends_hold ) {
        scroll_frame = 0;
      }

      // Hold at end
      offset = text_overflow;
    }

    for (int i = 0; i < line_count; i++) {
      int x = -offset - i*display_width;
      int custom_font_offset = font != NULL ? font_height : 0;
      int y = starting_y + custom_font_offset + i*y_advance;

      display.setCursor(x, y);
      display.print(text);

      // Write only the first frame if not scrolling because only the first
      // line needs to be written when not scrolling because it'll just wrap.
      if (!needs_scrolling)
        break;
    }

    return true;
  }
};

// Which font to use. Change to NULL for default 6x8 font.
const GFXfont *font = &FreeSansBold9pt7b;
//const GFXfont *font = NULL;
const uint8_t text_size = 1;
const uint8_t scroll_frame_count = 1;

ScrollArea topLines(0, 48, font, text_size, scroll_frame_count);
ScrollArea bottomLine(48, 16, font, text_size, scroll_frame_count);

bool display_setup()
{
  static bool successful = false;
  if (successful)
    return true;

  // Address 0x3C default
  if (!display.begin(0x3C, true)) {
    led_blinkCode(no_display);
    return false;
  }

  display.setRotation(1);
  display.setTextColor(SH110X_WHITE);
  display.clearDisplay();

  // Don't initialize again.
  successful = true;

  return true;
}

/*
 * Writes given text to display, scrolling on subsequent calls it if it's too big to fit.
 * top displays within 3 lines; bottom within 1.
 * Returns whether the display was updated.
 */
bool display_text(const char* top, const char* bottom)
{
  // Avoid lazy evaluation to ensure both lines evaluate whether to update.
  bool display_changed = topLines.Display(top);
  display_changed |= bottomLine.Display(bottom);

  if (display_changed)
    display.display();

  return display_changed;
}
