#include <constants.h>
#include <display.h>
#include <encoder.h>
#include <led.h>
#include <mass_storage.h>
#include <vs1053.h>

#include <Adafruit_SleepyDog.h>
#include <Arduino.h>
#include <Wire.h>
#include <algorithm>
#include <ArduinoTrace.h>

// Updating the display is usually at or just under this duration.
const unsigned long target_frametime_micros = 70000;

// Blink codes for startup situations
const int waiting_for_serial[] = {short_blink_ms, 0};

void setup()
{
  mass_storage_setup();

  Serial.begin(115200);

#if 0
  // Blink while waiting for serial connection
  if (!Serial) {
    display_setup();
    do {
      display_text("Waiting for serial", booting);
      led_blinkCode(waiting_for_serial);
    } while (!Serial);
  }

  Serial.println("Starting up");
#endif

  while (!display_setup())
    Serial.println("Display setup failed");

  display_text("", booting);

  Wire.begin();

  while (!encoder_setup())
    Serial.println("Cannot find encoder");

  while (!vs1053_setup())
    Serial.println("VS1053 setup failed");

  // Enable watchdog before entering loop()
  int countdown_milliseconds = Watchdog.enable(4000);
  Serial.print("Watchdog timer set for ");
  Serial.print(countdown_milliseconds);
  Serial.println(" milliseconds");

  Serial.println("Startup complete");
  led_off();
  encoder_led_off();

  // Start the first song.
  vs1053_changeSong(0);
}

void loop()
{
  static unsigned long frame_times[2000] = {};
  static int frame_time_index = 0;
  static unsigned long idle_frame_times[2000] = {};
  static int idle_frame_time_index = 0;
  const unsigned long frame_time_report_interval_ms = 5000;
  static unsigned long last_frame_time_report;
  static bool paused = false;

  unsigned long start_micros = micros();
  unsigned long start = millis();

  Watchdog.reset();

  // Switch to mass storage mode on button press. This is a separate mode so
  // that music playback isn't interrupted by mass storage CPU load.
  // mass_storage_loop() does not return.
  if (massStorageMode) {
    if (!paused) vs1053_togglePause();
    mass_storage_loop();
  }

  // Toggle pause on encoder button press.
  if (encoder_togglePause()) {
    paused = !paused;
    vs1053_togglePause();
  } else if (!paused) {
    auto change = encoder_getChange();
    if (change != 0)
      vs1053_changeSong(change);
  }

  bool display_updated = vs1053_loop();

  unsigned long end = millis();
  unsigned long frame_time = end - start;
  bool interval_report = end - last_frame_time_report >= frame_time_report_interval_ms;
  bool buffer_full = frame_time_index == COUNT_OF(frame_times);
  bool idle_buffer_full = idle_frame_time_index == COUNT_OF(idle_frame_times);
  if (interval_report) {
    unsigned long total = 0;
    unsigned long max_display = 0;
    for (int i = 0; i < frame_time_index; i++) {
      total += frame_times[i];
      if (frame_times[i] > max_display) max_display = frame_times[i];
    }

    unsigned long idle_total = 0;
    unsigned long max_idle = 0;
    for (int i = 0; i < idle_frame_time_index; i++) {
      idle_total += idle_frame_times[i];
      if (idle_frame_times[i] > max_idle) max_idle = idle_frame_times[i];
    }

    Serial.printf("Update duration: DISPLAY mean %02.1f ms, max %lu ms | IDLE %02.1f ms, max %lu ms \r\n",
                  total / max((float)frame_time_index, 1.0f),
                  max_display,
                  idle_total / max((float)idle_frame_time_index, 1.0f),
                  max_idle);

    last_frame_time_report = end;

    // Don't reconsider the same collection of frame times.
    buffer_full = true;
    idle_buffer_full = true;
  }

  if (buffer_full) frame_time_index = 0;
  if (idle_buffer_full) frame_time_index = 0;

  // Present averages only of display update duration
  if (display_updated) frame_times[frame_time_index++] = frame_time;
  else idle_frame_times[idle_frame_time_index++] = frame_time;

  // If this frame completed faster than the target, wait before starting the
  // next.
  auto micros_frame_time = micros() - start_micros;
  if (micros_frame_time < target_frametime_micros) {
    delayMicroseconds(target_frametime_micros - micros_frame_time);
  } else {
    Serial.printf("Long frame! %lu us\r\n", micros_frame_time);
  }
}
