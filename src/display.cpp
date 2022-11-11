#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <Fonts/FreeSansBold9pt7b.h>

const int display_width = 128;
const int display_height = 64;

Adafruit_SH1107 display = Adafruit_SH1107(display_height, display_width, &Wire);

const int previous_text_str_len = 255;

class ScrollArea {
private:
  int16_t starting_y;
  int height;
  int scroll_frame_interval;

  char previous_text[previous_text_str_len + 1] = {};
  bool needs_scrolling;
  int scroll_frame;
  uint16_t total_text_width;
  uint16_t font_height;
  const GFXfont *font;
  int font_scale;
public:
  // Scroll speed - frames to wait between scroll movements. See target_frametime.
  ScrollArea(int16_t starting_y, int height, const GFXfont *font, int font_scale, int scroll_frame_interval) {
    this->starting_y = starting_y;
    this->height = height;
    this->font = font;
    this->font_scale = font_scale;
    this->scroll_frame_interval = scroll_frame_interval;
  }

  // Display text, and scroll if necessary on repeated calls.
  // Returns true if the display was updated.
  bool Display(const char* text) {
    bool text_changed = strcmp(previous_text, text);
    if (!text_changed && !needs_scrolling)
      return false;
    
    display.setTextWrap(false);
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
      display.getTextBounds(text, 0, 0, &x1, &y1, &total_text_width, &font_height);

      int lines = height / font_height;

      needs_scrolling = total_text_width > 0.7*(display_width * lines);

      //Serial.printf("Displaying '%s' - font height %hu, %d lines, scrolling %d\r\n", text, font_height, lines, needs_scrolling);
      //Serial.printf("Text changed to '%s'\r\n", text);
    } else {
      // No text change, but may need to scroll if the frame advanced past the interval.
      scroll_frame++;
      if ((scroll_frame - 1) / scroll_frame_interval == scroll_frame / scroll_frame_interval)
        return false;
    }

    // Before updating, clear previously displayed text within the area.
    display.fillRect(0, starting_y, display_width, height, SH110X_BLACK);
    //display.clearDisplay();

    int offset = scroll_frame / scroll_frame_interval;

    // Hold for this many frame intervals at the start/end of the string instead of immediately continuing.
    const int ends_hold = 14;
    if (offset <= ends_hold) {
      // Hold at start
      offset = 0;
    } else if (offset <= total_text_width) {
      // Compensate for holding at start
      offset -= ends_hold;
    } else if (offset > total_text_width + ends_hold) {
      // Hold at end
      offset = total_text_width;
    } else {
      // Entire line has been displayed. Reset scrolling.
      // TODO: this may need to consider how much the display can fit.
      scroll_frame = 0;
    }

    for (int i = 0; i < height / font_height; i++) {
      int x = -offset - i*display_width;
      int custom_font_offset = font != NULL ? font_height : 0;
      int y = starting_y + custom_font_offset + i*font_height;

      display.setCursor(x, y);
      display.print(text);
    }

    return true;
  }
};

// FreeSansBold9pt7b has a font height of 13.
// Default font at 2x has one of 16.

ScrollArea topLines(0, 48, &FreeSansBold9pt7b, 1, 1);
ScrollArea bottomLine(48, 16, &FreeSansBold9pt7b, 1, 1);

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
  display.setFont(&FreeSansBold9pt7b);
  display.setTextColor(SH110X_WHITE);

  return true;
}

/*
 * Writes given text to display, scrolling on subsequent calls it if it's too big to fit.
 * top displays within 3 lines; bottom within 1.
 * Returns whether the display was updated.
 */
bool display_text(const char* top, const char* bottom)
{
  //bool display_changed = topLines.Display(top) || bottomLine.Display(bottom); // OOPS - took me a while to realize lazy eval was biting me
  bool display_changed = topLines.Display(top);
  display_changed |= bottomLine.Display(bottom);

  if (display_changed)
    display.display();
  
  return display_changed;
}
