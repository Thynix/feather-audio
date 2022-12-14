#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <Adafruit_VS1053.h>
#include <patching.h>
#include <Debouncer.h>
#include <Wire.h>
#include <Adafruit_seesaw.h>
#include <seesaw_neopixel.h>
#include <feather_pins.h>
#include <vector>
#include <display.h>
#include <algorithm>
#define MP3_ID3_TAGS_IMPLEMENTATION
#include <mp3_id3_tags.h>
#include <Adafruit_SleepyDog.h>
#include <Adafruit_TinyUSB.h>
#include <mass_storage.h>

#define COUNT_OF(x) ((sizeof(x)/sizeof(0[x])) / ((size_t)(!(sizeof(x) % sizeof(0[x])))))

File file;

void populateFilenames(File);
void blinkCode(const int *);
float readVolume();

Adafruit_VS1053_FilePlayer musicPlayer =
  Adafruit_VS1053_FilePlayer(VS1053_RESET, VS1053_CS, VS1053_DCS, VS1053_DREQ, CARDCS);

const int debounce_ms = 100;
Debouncer encoderButton(debounce_ms);
Debouncer massStorageButton(debounce_ms);

const uint8_t volume_pin = A2;
const uint8_t mass_storage_pin = 12;

const int fileStackSize = 5;
int fileIndex = 0;
File fileStack[fileStackSize] = {};
std::vector<const char*> filenames;
std::vector<const char*> display_names;

const size_t volumeReadCount = 100;
std::vector<uint32_t> volumeReads(volumeReadCount);

const char* const accepted_extensions[] = {
  ".MP3", ".mp3",
  ".OGG", ".ogg",
  ".FLA", ".fla",
  ".WAV", ".wav",
  ".M4A", ".m4a",
};

const uint8_t seesaw_addr = 0x36;
const uint8_t seesaw_switch_pin = 24;

Adafruit_seesaw ss;
seesaw_NeoPixel sspixel = seesaw_NeoPixel(1);

int32_t encoder_position;

const int short_blink_ms = 100;
const int long_blink_ms = 500;
const int after_pattern_ms = 1500;

// Updating the display is usually at or just under this duration.
const unsigned long target_frametime_micros = 70000;

// Blink codes for startup situations
const int waiting_for_serial[] = {short_blink_ms, 0};
const int no_seesaw[] = {long_blink_ms, 0};
const int wrong_seesaw[] = {long_blink_ms, short_blink_ms, 0};
const int no_VS1053[] = {short_blink_ms, long_blink_ms, 0};
const int no_microsd[] = {short_blink_ms, long_blink_ms, short_blink_ms, 0};
const int no_display[] = {long_blink_ms, short_blink_ms, short_blink_ms, 0};

// 160 is low enough to seem silent.
const uint8_t inaudible = 160;
const uint8_t silent = 255;
const uint8_t max_volume = 0;

// Boot status messages for the bottom line.
const char* const booting    = "Boot";
const char* const boot_error = "Boot error";

const char* const mass_storage_mode = "Mass storage mode";

void setup()
{
  pinMode(volume_pin, INPUT);
  pinMode(mass_storage_pin, INPUT_PULLUP);

  // Keep LED on during startup.
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  mass_storage_init();

  Serial.begin(115200);

#if 0
  // Blink while waiting for serial
  if (!Serial) {
    display_setup();
    do {
      display_text("Waiting for serial", booting);
      blinkCode(waiting_for_serial);
    } while (!Serial);
  }
#endif

  while (!display_setup()) {
    Serial.println("Display setup failed");
    blinkCode(no_display);
  }
  display_text("", booting);

  Wire.begin();

  // Search for Seesaw device
  if (!ss.begin(seesaw_addr) || !sspixel.begin(seesaw_addr)) {
    display_text("Cannot find encoder", boot_error);
    while(true) blinkCode(no_seesaw);
  }

  // Check that found device is a rotary encoder
  uint32_t version = ((ss.getVersion() >> 16) & 0xFFFF);
  if (version  != 4991) {
    Serial.print("Wrong firmware loaded? Instead of rotary encoder, found product #");
    Serial.println(version);

    display_text("Wrong encoder version", boot_error);
    while(true) blinkCode(wrong_seesaw);
  }

  // Set encoder pixel green while booting
  sspixel.setPixelColor(0, 0x00ff00);
  sspixel.show();

  // Use a pin for the built in encoder switch
  ss.pinMode(seesaw_switch_pin, INPUT_PULLUP);

  // Get starting position
  encoder_position = ss.getEncoderPosition();

  delay(10);
  ss.setGPIOInterrupts((uint32_t)1 << seesaw_switch_pin, 1);
  ss.enableEncoderInterrupt();

  // Initialize music player
  for (int tries = 0; !musicPlayer.begin(); tries++) {
    if (tries == 5) {
      display_text("Failed to find VS105", boot_error);
      while (true) blinkCode(no_VS1053);
    }

    display_text("Retrying VS1053", booting);
    blinkCode(no_VS1053);
  }

  // Initialize SD card
  if (!SD.begin(CARDCS)) {
    display_text("MicroSD failed or not present", boot_error);

    while (true) blinkCode(no_microsd);
  }

  display_text("Patching VS1053", booting);
  musicPlayer.applyPatch(plugin, pluginSize);

  display_text("Finding songs", booting);
  unsigned long load_start = millis();
  auto root = SD.open("/");
  populateFilenames(root);
  root.close();

  struct {
    bool operator()(const char* a, const char* b) { return strcmp(a, b) < 0; }
  } compareStrings;

  // Present songs in lexicographic filename order
  std::sort(filenames.begin(), filenames.end(), compareStrings);

  if (filenames.size() == 0) {
    display_text("No songs found", boot_error);
    while (true) blinkCode(no_microsd);
  }

  // Load song tags
  // TODO: do this lazily? nah, it'd make things less responsive once it's running.
  char buf[128] = {};
  display_names.reserve(filenames.size());
  for (size_t i = 0; i < filenames.size(); i++) {
    snprintf(buf, sizeof(buf), "Loading song %u / %u", i + 1, filenames.size());
    display_text(buf, booting);

    Serial.printf("%12s | ", filenames[i]);

    auto file = SD.open(filenames[i]);
    if (mp3_id3_file_has_tags(&file)) {
      const char* title = mp3_id3_file_read_tag(&file, MP3_ID3_TAG_TITLE);
      const char* album = mp3_id3_file_read_tag(&file, MP3_ID3_TAG_ALBUM);
      const char* artist = mp3_id3_file_read_tag(&file, MP3_ID3_TAG_ARTIST);
      const size_t display_name_len = 512 + 1;
      char *display_name = (char*)malloc(display_name_len);

      // Songs are liable to not have an album set if manually tagged.
      if (strlen(album)) {
        snprintf(display_name, display_name_len, "%s by %s in %s", title, artist, album);
      } else {
        snprintf(display_name, display_name_len, "%s by %s", title, artist);
      }

      free((void*)title);
      free((void*)artist);
      display_names[i] = display_name;
    } else {
      // Remove extension from filename in the absence of tags
      // +1 for null terminator; -4 for ".mp3" or similar
      size_t len = strlen(filenames[i]) + 1 - 4;
      char *display_name = (char*) malloc(len);
      strncpy(display_name, filenames[i], len);
      display_name[len - 1] = '\0';
      display_names[i] = display_name;
    }
    file.close();

    Serial.println(display_names[i]);
  }

  Serial.printf("Songs loaded in %lu ms\r\n", millis() - load_start);

  // Enable watchdog before entering loop()
  int countdown_milliseconds = Watchdog.enable(4000);
  Serial.print("Watchdog timer set for ");
  Serial.print(countdown_milliseconds);
  Serial.println(" milliseconds");

  // Turn LEDs off now that startup is complete
  digitalWrite(LED_BUILTIN, LOW);
  sspixel.setPixelColor(0, 0x000000);
  sspixel.show();
}

void loop()
{
  static bool paused = false;
  static int previous_display_volume = -1;
  static int selected_file_index = -1;
  static unsigned long frame_times[2000] = {};
  static int frame_time_index = 0;
  static unsigned long idle_frame_times[2000] = {};
  static int idle_frame_time_index = 0;
  const unsigned long frame_time_report_interval_ms = 5000;
  static unsigned long last_frame_time_report;
  static unsigned long song_start_time;
  static unsigned long song_pause_start;
  static unsigned long song_time_paused;
  static unsigned long last_volume_change;
  const unsigned long volume_change_display_ms = 1000;

  unsigned long start_micros = micros();
  unsigned long start = millis();

  Watchdog.reset();

  // Switch to mass storage mode on button press. This is a separate mode so
  // that music playback isn't interrupted by mass storage CPU load.
  if (massStorageButton.update(digitalRead(mass_storage_pin)) && !massStorageButton.get()) {
    Watchdog.disable();
    display_text(mass_storage_mode, booting);

    if (!mass_storage_begin(CARDCS)) {
      display_text("Mass storage failed", boot_error);

      while (true) blinkCode(no_microsd);
    }

    while (true) {
      display_text(mass_storage_mode, mass_storage_got_read ? "Got reads" : "No reads yet");
      delay(1000);
    }
  }

  // Because higher values given to musicPlayer.setVolume() are quieter, so
  // invert scaled ADC. Low ADC numbers give high volume values to be quiet.
  uint8_t volume = (uint8_t) ((1.0f - readVolume()) * inaudible);
  //uint8_t volume = (uint8_t) (0.5f * inaudible);

  // Only change volume setting if the displayed value is different.
  // 0 is 100%; 160 is 0%.
  int display_volume = roundf(100 - (100.0f/inaudible)*volume);
  if (previous_display_volume != display_volume) {
    Serial.printf("Set volume %d\n", volume);
    musicPlayer.setVolume(volume, volume);

    previous_display_volume = display_volume;
    last_volume_change = start;
  }

  // Toggle pause on encoder button press.
  if (encoderButton.update(ss.digitalRead(seesaw_switch_pin)) && !encoderButton.get()) {
    paused = !paused;

    // Red if paused, otherwise off.
    if (paused) {
      song_pause_start = start;
      Serial.println("Pause");
      sspixel.setPixelColor(0, 0xff0000);
    } else {
      auto pause_duration = start - song_pause_start;
      song_time_paused += pause_duration;
      Serial.printf("Resumed after %lu ms\r\n", song_time_paused);
      sspixel.setPixelColor(0, 0x000000);
    }
    sspixel.show();

    musicPlayer.pausePlaying(paused);
  } else {
    // The encoder position doesn't have a valid reading when the button is
    // pressed, so consider it only when the button isn't pressed.
    auto new_position = ss.getEncoderPosition();
    auto encoder_change = new_position - encoder_position;
    encoder_position = new_position;

    if (!paused) {
    // Go to the next file on startup, after completing a song, or when requested.
    // The encoder may have moved multiple positions since the last check if
    // moving quickly, so consider each one.
    bool changed_song = true;
    if (musicPlayer.stopped() || encoder_change < 0) {
      Serial.printf("From %d next", selected_file_index);
      do {
        if ((size_t) selected_file_index >= filenames.size() - 1) {
          selected_file_index = 0;
        } else {
          selected_file_index++;
        }

        Serial.print(' ');
        Serial.print(encoder_change);
      } while (++encoder_change < 0);
      Serial.printf(" to %d\n", selected_file_index);
    } else if (encoder_change > 0) {
      Serial.printf("From %d previous", selected_file_index);
      do {
        if (selected_file_index <= 0) {
          selected_file_index = filenames.size() - 1;
        } else {
          selected_file_index--;
        }

        Serial.print(' ');
        Serial.print(encoder_change);
      } while (--encoder_change > 0);
      Serial.printf(" to %d\n", selected_file_index);
    } else {
      changed_song = false;
    }

    if (changed_song) {
      Serial.printf("Filename: '%s'\r\n", filenames[selected_file_index]);
      Serial.printf("Display name: '%s'\r\n", display_names[selected_file_index]);
      musicPlayer.stopPlaying();
      musicPlayer.softReset();
      if (!musicPlayer.startPlayingFile(filenames[selected_file_index])) {
        display_text(filenames[selected_file_index], "start failed");
        delay(1000);
        musicPlayer.stopPlaying();
      }

      song_start_time = start;
      song_time_paused = 0;
    }
    }
  }

  bool display_updated = false;
  if (start - last_volume_change < volume_change_display_ms) {
      char buf[32];
      // Pad with two spaces to leave room for "100%"
      snprintf(buf, sizeof(buf), "    Vol %d%%", display_volume);

      display_updated = display_text(display_names[selected_file_index], buf);
  } else if (paused) {
    display_updated = display_text(display_names[selected_file_index],
                                  "    Paused");
  } else {
    char buf[32];

      int seconds_played = (start - song_start_time - song_time_paused) / 1000;
      // TODO: Instead of hardcoding %02d for song number, determine digits in song count and match it.
      // Playtime in minutes:seconds song number/song count
      snprintf(buf, sizeof(buf), "%d:%02d %02d/%u",
               seconds_played / 60, seconds_played % 60,
               selected_file_index + 1, filenames.size());

    display_updated = display_text(display_names[selected_file_index], buf);
  }

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

  // Continue feeding the buffer while waiting for the next frame.
  auto micros_frame_time = micros() - start_micros;
  if (micros_frame_time < target_frametime_micros) {
    auto wait_duration_remaining = target_frametime_micros - micros_frame_time;

    // Wait until next frame, feeding the buffer every poll interval.
    const unsigned long feed_poll_microseconds = 5000;
    while (wait_duration_remaining) {
      auto feed_start_micros = micros();
      musicPlayer.feedBuffer();

      // Don't re-wait the time it took to feed the buffer.
      auto feed_duration_micros = micros() - feed_start_micros;
      if (wait_duration_remaining > feed_duration_micros) {
        wait_duration_remaining -= feed_duration_micros;
      } else {
        // No more waiting when the duration is elapsed.
        break;
      }

      auto wait_duration = min(feed_poll_microseconds, wait_duration_remaining);
      delayMicroseconds(wait_duration);
      wait_duration_remaining -= wait_duration;
    }
  } else {
    Serial.printf("Long frame! %.1f ms (display updated %d)\r\n", (micros() - start_micros) / 1000.0f, display_updated);
  }
}

void populateFilenames(File dir)
{
    while(true) {
      File entry =  dir.openNextFile();
      if (!entry) {
        // no more files
        break;
      }

      // Recurse if necessary.
      // TODO: fix recursed paths
      if (entry.isDirectory()) {
        //populateFilenames(entry);
        entry.close();
        continue;
      }

      char nameBuf[128] = {};
      strcpy(nameBuf, entry.name());

      // Check whether it's a supported extension. (MP3 is best supported.)
      for (size_t i = 0; i < COUNT_OF(accepted_extensions); i++) {
        if (strstr(nameBuf, accepted_extensions[i])) {
          goto accept_entry;
        }
      }

      // Ignore it if it doesn't have a usable extension.
      entry.close();
      continue;

accept_entry:
      // Duplicate so the entry can be closed.
      char *name = (char*) malloc(strlen(nameBuf) + 1);
      strcpy(name, nameBuf);
      filenames.push_back(name);

      entry.close();
  }
}

float readVolume()
{
  volumeReads.clear();

  for (size_t i = 0; i < volumeReadCount; i++)
    volumeReads.push_back(analogRead(volume_pin));

  struct {
    bool operator()(uint32_t a, uint32_t b) { return a < b; }
  } compareReads;

  std::sort(volumeReads.begin(), volumeReads.end(), compareReads);
  uint32_t medianRead = volumeReads[volumeReads.size() / 2];

  // When using a linear potentiometer, take the log to match volume perception.
  // ADC max is 1023, and natural log of 1023 = 6.93049
  float scaledADC = log(max(1023 - medianRead, 1u)) / 6.93049;

  // Audio potentiometer - no scaling.
  //float scaledADC = analogRead(volume_pin) / 1023.0;

  if (scaledADC < 0.0f) scaledADC = 0.0f;
  if (scaledADC > 1.0f) scaledADC = 1.0f;

  return scaledADC;
}

void blinkCode(const int *delays)
{
  // Blink (odd off, even on) until encountering 0 length terminator.
  for (int i = 0; delays[i]; i++) {
    if (i % 2) digitalWrite(LED_BUILTIN, LOW);
    else       digitalWrite(LED_BUILTIN, HIGH);

    delay(delays[i]);
  }

  digitalWrite(LED_BUILTIN, LOW);
  delay(after_pattern_ms);
}
