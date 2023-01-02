#include <vs1053.h>

#include <constants.h>
#include <display.h>
#include <led.h>
#include <patching.h>

#define MP3_ID3_TAGS_IMPLEMENTATION
#include <mp3_id3_tags.h>

#include <Adafruit_VS1053.h>
#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <algorithm>
#include <stdint.h>
#include <vector>

const char* const accepted_extensions[] = {
  ".MP3", ".mp3",
  ".OGG", ".ogg",
  ".FLA", ".fla",
  ".WAV", ".wav",
  ".M4A", ".m4a",
};

const int no_VS1053[] = {short_blink_ms, long_blink_ms, 0};

// 160 is low enough to seem silent.
const uint8_t inaudible = 160;
const uint8_t silent = 255;
const uint8_t max_volume = 0;

const uint8_t VS1053_RESET = -1;     // VS1053 reset pin (not used!)

// Feather ESP8266
#if defined(ESP8266)
const uint8_t VS1053_CS = 16;      // VS1053 chip select pin (output)
const uint8_t VS1053_DCS = 15;     // VS1053 Data/command select pin (output)
const uint8_t VS1053_DREQ = 0;     // VS1053 Data request, ideally an Interrupt pin

// Feather ESP32
#elif defined(ESP32) && !defined(ARDUINO_ADAFRUIT_FEATHER_ESP32S2)
const uint8_t VS1053_CS = 32;      // VS1053 chip select pin (output)
const uint8_t VS1053_DCS = 33;     // VS1053 Data/command select pin (output)
const uint8_t VS1053_DREQ = 15;    // VS1053 Data request, ideally an Interrupt pin

// Feather Teensy3
#elif defined(TEENSYDUINO)
const uint8_t VS1053_CS = 3;       // VS1053 chip select pin (output)
const uint8_t VS1053_DCS = 10;     // VS1053 Data/command select pin (output)
const uint8_t VS1053_DREQ = 4;     // VS1053 Data request, ideally an Interrupt pin

// WICED feather
#elif defined(ARDUINO_STM32_FEATHER)
const uint8_t VS1053_CS = PC7;     // VS1053 chip select pin (output)
const uint8_t VS1053_DCS = PB4;    // VS1053 Data/command select pin (output)
const uint8_t VS1053_DREQ = PA15;  // VS1053 Data request, ideally an Interrupt pin

#elif defined(ARDUINO_NRF52832_FEATHER)
const uint8_t VS1053_CS = 30;      // VS1053 chip select pin (output)
const uint8_t VS1053_DCS = 11;     // VS1053 Data/command select pin (output)
const uint8_t VS1053_DREQ = 31;    // VS1053 Data request, ideally an Interrupt pin

// Feather M4, M0, 328, ESP32-S2, nRF52840 or 32u4
#else
const uint8_t VS1053_CS = 6;       // VS1053 chip select pin (output)
const uint8_t VS1053_DCS = 10;     // VS1053 Data/command select pin (output)
  // DREQ should be an Int pin *if possible* (not possible on 32u4)
const uint8_t VS1053_DREQ = 9;     // VS1053 Data request, ideally an Interrupt pin
const uint8_t volume_pin = A2;

#endif

Adafruit_VS1053_FilePlayer musicPlayer =
  Adafruit_VS1053_FilePlayer(VS1053_RESET, VS1053_CS, VS1053_DCS, VS1053_DREQ, CARDCS);

File file;

const size_t volumeReadCount = 300;
std::vector<uint32_t> volumeReads(volumeReadCount);

const int fileStackSize = 5;
File fileStack[fileStackSize] = {};
std::vector<const char*> filenames;
std::vector<const char*> display_names;
int selected_file_index = 0;

unsigned long song_start_millis;
unsigned long song_millis_paused;
bool paused = false;

void populateFilenames(File);
float readVolume();

bool vs1053_setup()
{
  if (!musicPlayer.begin()) {
    display_text("Failed to find VS105", boot_error);
    led_blinkCode(no_VS1053);
    return false;
  }

  if (!SD.begin(CARDCS)) {
    display_text("MicroSD failed or not present", boot_error);
    led_blinkCode(no_microsd);
    return false;
  }

  display_text("Patching         VS1053", booting);
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
    while (true) led_blinkCode(no_microsd);
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

  Serial.print("Songs loaded in ");
  Serial.print(millis() - load_start);
  Serial.println(" ms");

  // Wait for... settling? Without this serial is liable to disconnect with:
  //     kernel: usb usb5-port2: disabled by hub (EMI?), re-enabling...
  delay(100);

  // DREQ is on an interrupt pin, so use background audio playing
  if (!musicPlayer.useInterrupt(VS1053_FILEPLAYER_PIN_INT)) {
    Serial.println("Failed to set VS1053 interrupt");
    display_text("VS1053 interrupts error", boot_error);
    led_blinkCode(no_VS1053);
    return false;
  }

  return true;
}

bool vs1053_loop()
{
  static int previous_display_volume = -1;
  static unsigned long last_volume_change;
  const unsigned long volume_change_display_ms = 1000;

  unsigned long start = millis();

  // Because higher values given to musicPlayer.setVolume() are quieter, so
  // invert scaled ADC. Low ADC numbers give high volume values to be quiet.
  // Pot
  //uint8_t volume = (uint8_t) ((1.0f - readVolume()) * inaudible);
  // Reverse pot
  uint8_t volume = (uint8_t) (readVolume() * inaudible);

  // Only change volume setting if the displayed value is different.
  // 0 is 100%; 160 is 0%.
  int display_volume = roundf(100 - (100.0f/inaudible)*volume);
  if (previous_display_volume != display_volume) {
    // Ignore single-percentage changes when not already displaying volume.
    // This means the minimum adjustment to start adjusting is 2.
    // (With the exception of 0% and 100% as those have a wider stable range.)
    if (start - last_volume_change >= volume_change_display_ms &&
        abs(previous_display_volume - display_volume) == 1 &&
        display_volume != 0 && display_volume != 100) {
      //Serial.println("Ignoring volume flicker");
    } else {
      Serial.printf("Set volume %d\n", volume);
      musicPlayer.setVolume(volume, volume);

      previous_display_volume = display_volume;
      last_volume_change = start;
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
    auto seconds_played = musicPlayer.decodeTime();
    char buf[32];

    // TODO: Instead of hardcoding %02d for song number, determine digits in song count and match it.
    // Playtime in minutes:seconds song number/song count
    snprintf(buf, sizeof(buf), "%d:%02d %02d/%u",
             seconds_played / 60, seconds_played % 60,
             selected_file_index + 1, filenames.size());

    display_updated = display_text(display_names[selected_file_index], buf);
  }

  // Advance to the next song upon completion.
  if (!paused && !musicPlayer.playingMusic)
    vs1053_changeSong(-1);

  return display_updated;
}

bool vs1053_changeSong(int encoder_change)
{
  if (encoder_change < 0) {
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
  }

  Serial.print("Filename: '");
  Serial.print(filenames[selected_file_index]);
  Serial.println("'");
  Serial.print("Display name: '");
  Serial.print(display_names[selected_file_index]);
  Serial.println("'");
  musicPlayer.stopPlaying();

  // Clear decodeTime() so elapsed time doesn't accumulate between songs.
  musicPlayer.softReset();

  if (!musicPlayer.startPlayingFile(filenames[selected_file_index])) {
    musicPlayer.stopPlaying();

    for (int i = 0; i < 128; i++) {
      display_text(display_names[selected_file_index], "start failed");
    }
    return false;
  }

  song_start_millis = millis();
  song_millis_paused = 0;

  return true;
}

void vs1053_pause(bool pause)
{
  paused = pause;

  if (paused) {
    Serial.println("Pause");
  } else {
    Serial.println("Resume");
  }

  musicPlayer.pausePlaying(paused);
}

void vs1053_beep(uint16_t duration_ms, uint8_t frequency_code)
{
  musicPlayer.sineTest(frequency_code, duration_ms);
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

  // Linear potentiometer - take the log to match volume perception.
  // ADC max is 1023, and natural log of 1023 = 6.93049
  //float scaledADC = log(max(1023 - medianRead, 1u)) / 6.93049;

  // Audio potentiometer - no scaling.
  float scaledADC = medianRead / 1023.0;

  if (scaledADC < 0.0f) scaledADC = 0.0f;
  if (scaledADC > 1.0f) scaledADC = 1.0f;

  return scaledADC;
}

void populateFilenames(File dir)
{
    while(true) {
      File entry =  dir.openNextFile();
      if (!entry) {
        // no more files
        break;
      }

      // Don't recurse - only load top-level files.
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
