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

const char *const cacheFilename = "cache/cache.txt";

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

struct Song {
  String filename;
  String displayName;
};

Adafruit_VS1053_FilePlayer musicPlayer =
  Adafruit_VS1053_FilePlayer(VS1053_RESET, VS1053_CS, VS1053_DCS, VS1053_DREQ, CARDCS);

File file;

const size_t volumeReadCount = 600;
std::vector<uint32_t> volumeReads(volumeReadCount);

std::vector<Song> songs;
int selected_file_index = 0;

unsigned long song_start_millis;
unsigned long song_millis_paused;
bool paused = false;

float readVolume();
bool readCache();
bool writeCache();

bool vs1053_setup()
{
  static bool successful = false;
  if (successful)
    return true;

  if (!musicPlayer.begin()) {
    display_text("Failed to find VS1053", boot_error);
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

  // Don't initialize again.
  successful = true;

  return true;
}

const char *const importStatus = "Cache build";

void vs1053_importSongs()
{
  char buf[512] = {};
  int errors = 0;
  int i = 0;

  display_text("Import start", importStatus);

  auto root = SD.open("/");

  // Iterate root directory and load tags.
  for (auto file = root.openNextFile(); file; file.close(), file = root.openNextFile()) {
    if (file.isDirectory())
      continue;

    i++;

    bool error = false;
    if (strstr(file.name(), "\n"))  {
      errors++;
      error = true;
    }

    snprintf(buf, sizeof(buf), "Import song     %u", i + 1);

    // Append error count if relevant.
    if (errors)
      snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), " | %d errors", errors);

    display_text(buf, importStatus);

    Serial.printf("%12s | ", file.name());

    if (error) {
      Serial.println("error - name contains newlines");
      continue;
    }

    if (mp3_id3_file_has_tags(&file)) {
      const char* title = mp3_id3_file_read_tag(&file, MP3_ID3_TAG_TITLE);
      const char* album = mp3_id3_file_read_tag(&file, MP3_ID3_TAG_ALBUM);
      const char* artist = mp3_id3_file_read_tag(&file, MP3_ID3_TAG_ARTIST);

      // Songs are liable to not have an album set if manually tagged.
      if (strlen(album)) {
        snprintf(buf, sizeof(buf), "%s by %s in %s", title, artist, album);
      } else {
        snprintf(buf, sizeof(buf), "%s by %s", title, artist);
      }

      free((void*)title);
      free((void*)artist);
    } else {
      // Remove extension from filename in the absence of tags
      // +1 for null terminator; -4 for ".mp3" or similar
      size_t len = strlen(file.name()) + 1 - 4;
      strncpy(buf, file.name(), len);
      buf[len - 1] = '\0';
    }

    songs.push_back(Song{
      .filename = file.name(),
      .displayName = buf,
    });
  }

  root.close();

  struct {
    bool operator()(const Song &a, const Song &b) { return a.filename < b.filename; }
  } compareSongs;

  // Present songs in lexicographic filename order
  std::sort(songs.begin(), songs.end(), compareSongs);
}

void vs1053_loadSongs()
{
  display_text("Loading songs", booting);

  unsigned long load_start = millis();

  // Try to read the cache, but fall back to re-importing.
  bool usedCache = true;
  if (!readCache()) {
    usedCache = false;
    vs1053_importSongs();
    writeCache();
  }

  Serial.flush();
  Serial.print(songs.size());
  Serial.printf(" songs %s in ", usedCache ? "loaded from cache" : "imported");
  Serial.print(millis() - load_start);
  Serial.println(" milliseconds");

  // DREQ is on an interrupt pin, so use background audio playing
  if (!musicPlayer.useInterrupt(VS1053_FILEPLAYER_PIN_INT)) {
    Serial.println("failed to set VS1053 interrupt");
    display_text("VS1053 interrupt setup failed", boot_error);
    while (true) led_blinkCode(no_VS1053);
  }
}

bool vs1053_loop()
{
  static int previous_display_volume = -1;
  static unsigned long last_volume_change;
  const unsigned long volume_change_display_ms = 1000;

  unsigned long start = millis();

  const auto displayName = songs[selected_file_index].displayName.c_str();

  // Because higher values given to musicPlayer.setVolume() are quieter, so
  // invert scaled ADC. Low ADC numbers give high volume values to be quiet.
  // Pot
  uint8_t volume = (uint8_t) ((1.0f - readVolume()) * inaudible);
  // Reverse pot
  //uint8_t volume = (uint8_t) (readVolume() * inaudible);
  // Hardcode when pot not connected.
  //uint8_t volume = (uint8_t) (0.3 * inaudible);

  // Only change volume setting if the displayed value is different.
  // 100% volume is 0
  // 0% volume is inaudible
  int display_volume = roundf(100 - (100.0f/inaudible)*volume);
  if (previous_display_volume != display_volume) {
    Serial.printf("Volume %d%%: %d\n", display_volume, volume);
    musicPlayer.setVolume(volume, volume);

    previous_display_volume = display_volume;
    last_volume_change = start;
  }

  bool display_updated = false;
  if (start - last_volume_change < volume_change_display_ms) {
      char buf[32];
      // Pad with two spaces to leave room for "100%"
      snprintf(buf, sizeof(buf), "    Vol %d%%", display_volume);

      display_updated = display_text(displayName, buf);
  } else if (paused) {
    display_updated = display_text(displayName,
                                  "    Paused");
  } else {
    auto seconds_played = musicPlayer.decodeTime();
    char buf[32];

    // TODO: Instead of hardcoding %02d for song number, determine digits in song count and match it.
    // Playtime in minutes:seconds song number/song count
    snprintf(buf, sizeof(buf), "%d:%02d %02d/%u",
             seconds_played / 60, seconds_played % 60,
             selected_file_index + 1, songs.size());

    display_updated = display_text(displayName, buf);
  }

  // Advance to the next song upon completion.
  if (!paused && !musicPlayer.playingMusic)
    vs1053_changeSong(1);

  return display_updated;
}

bool vs1053_changeSong(int encoder_change)
{
  Serial.print("Moving ");
  Serial.print(encoder_change);

  selected_file_index += encoder_change;

  // Wrap around playlist when negative.
  while (selected_file_index < 0) {
    selected_file_index += songs.size();
  }

  // Wrap around playlist when beyond its length.
  selected_file_index = selected_file_index % songs.size();

  auto selectedSong = songs[selected_file_index];

  Serial.print(" to '");
  Serial.print(selectedSong.displayName);
  Serial.println("'");

  musicPlayer.stopPlaying();

  // Clear decodeTime() so elapsed time doesn't accumulate between songs.
  musicPlayer.softReset();

  if (!musicPlayer.startPlayingFile(selectedSong.filename.c_str())) {
    musicPlayer.stopPlaying();

    for (int i = 0; i < 128; i++) {
      display_text(selectedSong.displayName.c_str(), "start failed");
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

  // Discard noise; leave 7 bits to allow meaningful percentage.
  int truncatedRead = medianRead >> 3;

  // Linear potentiometer - take the log to match volume perception.
  // ADC read is truncated to a maximum of 127, and natural log of 127 = 4.844187086458591
  //float scaledADC = log(max(127 - truncatedRead, 1u)) / 4.844187086458591;

  // Audio potentiometer - no scaling.
  float scaledADC = truncatedRead / 127.0;

  if (scaledADC < 0.0f) scaledADC = 0.0f;
  if (scaledADC > 1.0f) scaledADC = 1.0f;

  return scaledADC;
}

void vs1053_clearSongCache()
{
  SD.remove(cacheFilename);
}

bool readCache()
{
  SD.mkdir("cache");

  auto cacheFile = SD.open(cacheFilename, FILE_READ);
  if (!cacheFile) {
    Serial.println("Failed to open cache file");
    return false;
  }

  display_text("Loading cache", booting);
  Serial.println("Loading cache");

  while (cacheFile.available()) {
    auto filename = cacheFile.readStringUntil('\n');
    auto displayName = cacheFile.readStringUntil('\n');

    Serial.printf("%12s | ", filename);
    Serial.println(displayName);

    songs.push_back(Song{
      .filename = filename,
      .displayName = displayName,
    });
  }

  cacheFile.close();
  return true;
}

bool writeCache()
{
  auto cacheFile = SD.open(cacheFilename, FILE_WRITE);
  if (!cacheFile) {
    Serial.println("Failed to open cache file");
    return false;
  }

  for (auto song : songs) {
    cacheFile.write(song.filename.c_str());
    cacheFile.write('\n');
    cacheFile.write(song.displayName.c_str());
    cacheFile.write('\n');
  }

  cacheFile.close();

  Serial.println("Wrote cache");

  return true;
}
