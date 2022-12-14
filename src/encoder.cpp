#include <encoder.h>

#include <constants.h>
#include <display.h>
#include <led.h>

#include <Debouncer.h>
#include <Adafruit_seesaw.h>
#include <seesaw_neopixel.h>

const uint8_t seesaw_addr = 0x36;
const uint8_t seesaw_switch_pin = 24;

const int no_seesaw[] = {long_blink_ms, 0};
const int wrong_seesaw[] = {long_blink_ms, short_blink_ms, 0};

Adafruit_seesaw ss;
seesaw_NeoPixel sspixel = seesaw_NeoPixel(1);

int32_t encoder_position;

Debouncer encoderButton(debounce_ms);

bool encoder_setup()
{
    // Search for Seesaw device
  if (!ss.begin(seesaw_addr) || !sspixel.begin(seesaw_addr)) {
    display_text("Cannot find encoder", boot_error);
    led_blinkCode(no_seesaw);
    return false;
  }

  // Check that found device is a rotary encoder
  uint32_t version = ((ss.getVersion() >> 16) & 0xFFFF);
  if (version  != 4991) {
    Serial.print("Wrong firmware loaded? Instead of rotary encoder, found product #");
    Serial.println(version);

    display_text("Wrong encoder version", boot_error);
    while(true) led_blinkCode(wrong_seesaw);
  }

  // Set encoder pixel green while booting
  sspixel.setPixelColor(0, 0x00ff00);
  sspixel.show();

  // Use a pin for the built in encoder switch
  ss.pinMode(seesaw_switch_pin, INPUT_PULLUP);

  // Get starting position
  encoder_position = ss.getEncoderPosition();

  ss.setGPIOInterrupts((uint32_t)1 << seesaw_switch_pin, 1);
  ss.enableEncoderInterrupt();

  return true;
}

bool encoder_togglePause()
{
  static bool paused = false;

  if (!(encoderButton.update(ss.digitalRead(seesaw_switch_pin)) && !encoderButton.get()))
    return false;

  paused = !paused;

  // Red if paused, and off if not.
  if (paused) {
    sspixel.setPixelColor(0, 0xff0000);
    sspixel.show();
  } else {
    encoder_led_off();
  }

  return true;
}

int encoder_getChange()
{
  auto new_position = ss.getEncoderPosition();
  auto encoder_change = new_position - encoder_position;
  encoder_position = new_position;

  return encoder_change;
}

void encoder_led_off()
{
  sspixel.setPixelColor(0, 0x000000);
  sspixel.show();
}
