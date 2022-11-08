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

#define COUNT_OF(x) ((sizeof(x)/sizeof(0[x])) / ((size_t)(!(sizeof(x) % sizeof(0[x])))))

File file;

void populateFilenames(File);
void blinkCode(const int *);
float readVolume();
bool waitForSerial();

Adafruit_VS1053_FilePlayer musicPlayer =
  Adafruit_VS1053_FilePlayer(VS1053_RESET, VS1053_CS, VS1053_DCS, VS1053_DREQ, CARDCS);

const int debounce_ms = 100;
Debouncer encoderButton(debounce_ms);
Debouncer toggleLeftChannelButton(debounce_ms);
Debouncer toggleRightChannelButton(debounce_ms);

const uint8_t volume_pin = A2;

// Feather M4 pins
// Also used for VS1053 - can't use these while using it.
const uint8_t button_a_pin = 9;
const uint8_t button_b_pin = 6;
const uint8_t button_c_pin = 5;

const int fileStackSize = 5;
int fileIndex = 0;
File fileStack[fileStackSize] = {};
std::vector<const char*> filenames;
std::vector<const char*> display_names;

const char* const accepted_extensions[] = {
  ".MP3", ".mp3",
  ".OGG", ".ogg",
  ".FLA", ".fla",
};

const uint8_t seesaw_addr = 0x36;
const uint8_t seesaw_switch_pin = 24;

Adafruit_seesaw ss;
seesaw_NeoPixel sspixel = seesaw_NeoPixel(1);

int32_t encoder_position;

const int short_blink_ms = 100;
const int long_blink_ms = 500;
const int after_pattern_ms = 1500;

const unsigned long target_frametime = 15;

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

void setup()
{
  // Keep LED on during startup.
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  Serial.begin(115200);

  pinMode(volume_pin, INPUT);
  pinMode(button_a_pin, INPUT_PULLUP);

  if (!display_setup()) {
    while(true) blinkCode(no_display);
  }
  display_text("", "Booting...");

  // Blink while waiting for serial
  if (waitForSerial()) {
    // Avoid the delay of writing to the display each loop
    display_text("Waiting for serial", "Booting...");

    while(waitForSerial()) blinkCode(waiting_for_serial);
  }

  Wire.begin();

  // Search for Seesaw device
  if (!ss.begin(seesaw_addr) || !sspixel.begin(seesaw_addr)) {
    display_text("Cannot find encoder", "");
    while(true) blinkCode(no_seesaw);
  }

  // Check that found device is a rotary encoder
  uint32_t version = ((ss.getVersion() >> 16) & 0xFFFF);
  if (version  != 4991) {
    Serial.print("Wrong firmware loaded? Instead of rotary encoder, found product #");
    Serial.println(version);

    display_text("Wrong encoder version", "");
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
  if (!musicPlayer.begin()) {
    display_text("Cannot find VS1053", "");

    while (true) blinkCode(no_VS1053);
  }

  // Initialize SD card
  if (!SD.begin(CARDCS)) {
    display_text("MicroSD failed or not present", "");

    while (true) blinkCode(no_microsd);
  }

  display_text("Patching VS1053", "Booting...");
  musicPlayer.applyPatch(plugin, pluginSize);

  display_text("Loading songs", "Booting...");
  auto root = SD.open("/");
  populateFilenames(root);
  root.close();
  Serial.printf("Found %d songs\r\n", filenames.size());

  struct {
    bool operator()(const char* a, const char* b) { return strcmp(a, b) < 0; }
  } compareStrings;

  // Present songs in lexicographic filename order
  std::sort(filenames.begin(), filenames.end(), compareStrings);

  if (filenames.size() == 0) {
    display_text("No songs found", "");
    while (true) blinkCode(no_microsd);
  }

  // Load song tags
  display_names.reserve(filenames.size());
  for (size_t i = 0; i < filenames.size(); i++) {
    auto file = SD.open(filenames[i]);
    if (mp3_id3_file_has_tags(&file)) {
      const char* title = mp3_id3_file_read_tag(&file, MP3_ID3_TAG_TITLE);
      const char* artist = mp3_id3_file_read_tag(&file, MP3_ID3_TAG_ARTIST);
      const size_t display_name_len = 128;
      char *display_name = (char*)malloc(display_name_len);
      snprintf(display_name, display_name_len, "%s - %s", title, artist);
      free((void*)title);
      free((void*)artist);
      display_names[i] = display_name;
    } else {
      // Remove extension from filename
      char *filename = (char*) malloc(strlen(filenames[i]) + 1 - 4);
      strcpy(filename, filenames[i]);
      filename[strlen(filenames[i]) - 4] = '\0';
      free((void*)filenames[i]);
      display_names[i] = filename;
    }
  }

  display_text("Loaded songs", "Booting...");

  // DREQ is on an interrupt pin, so use background audio playing
  musicPlayer.useInterrupt(VS1053_FILEPLAYER_PIN_INT);

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
  static unsigned long frame_times[120] = {};
  static int frame_time_index = 0;

  unsigned long start = millis();

  // Because higher values given to musicPlayer.setVolume() are quieter, so
  // invert scaled ADC. Low ADC numbers give high volume values to be quiet.
  uint8_t volume = (uint8_t) ((1.0f - readVolume()) * inaudible);

  // Only change volume setting if the displayed value is different.
  // 0 is 100%; 160 is 0%.
  int display_volume = roundf(100 - (100/160.0)*volume);
  if (previous_display_volume != display_volume) {
    musicPlayer.setVolume(volume, volume);
    Serial.printf("Set volume %d\n", volume);

    previous_display_volume = display_volume;
  }

  // Toggle pause on encoder button press.
  if (encoderButton.update(ss.digitalRead(seesaw_switch_pin)) && !encoderButton.get()) {
    paused = !paused;
    musicPlayer.pausePlaying(paused);

    // Red if paused, otherwise off.
    if (paused) {
      Serial.println("Pause");
      sspixel.setPixelColor(0, 0xff0000);
    } else {
      Serial.println("Resume");
      sspixel.setPixelColor(0, 0x000000);
    }
    sspixel.show();
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

        encoder_change++;
      } while (encoder_change < 0);
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

        encoder_change--;
      } while (encoder_change > 0);
      Serial.printf(" to %d\n", selected_file_index);
    } else {
      changed_song = false;
    }

    if (changed_song) {
      /*
       * TODO: playing an MP3, then while that's playing trying to switch to an OGG has silent playback. When you try to play the OGG again it works.
       *       attempt to follow the datasheet in _datasheet_stopping broke stopping.
       */
      musicPlayer.stopPlaying();
      Serial.println(filenames[selected_file_index]);
      while (!musicPlayer.startPlayingFile(filenames[selected_file_index])) {
        display_text("Start failed", "");
        delay(100);
        musicPlayer.stopPlaying();
        display_text("Retrying", "");
        delay(100);
      }
    }
    }
  }

  if (paused) {
    display_text(display_names[selected_file_index], "   Paused");
  } else {
    char buf[32];
    // Pad with two spaces to leave room for "100%"
    sprintf(buf, "  Vol %d%%", display_volume);
    display_text(display_names[selected_file_index], buf);
  }

  // Display frame time information whenever frame time buffer fills.
  unsigned long frame_time = millis() - start;
  if (frame_time_index == COUNT_OF(frame_times)) {
    unsigned long total = 0;
    for (unsigned int i = 0; i < COUNT_OF(frame_times); i++) {
      total += frame_times[i];
    }

    Serial.printf("Average frame time %d ms\n", int(total / ((float)COUNT_OF(frame_times))));

    frame_time_index = 0;
  }
  frame_times[frame_time_index++] = frame_time;

  if (frame_time < target_frametime) {
    delay(target_frametime - frame_time);
  } else {
    Serial.printf("Long frame! %d ms\n", frame_time);
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
  const int volumeReads = 200;

  uint32_t readTotal = 0;
  for (int i = 0; i < volumeReads; i++)
    readTotal += analogRead(volume_pin);

  uint32_t averageRead = readTotal / ((float)volumeReads);

  // When using a linear potentiometer, take the log to match volume perception.
  // ADC max is 1023, and natural log of 1023 = 6.93049
  float scaledADC = log(max(1023 - averageRead, 1u)) / 6.93049;

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

bool waitForSerial() {
  return !Serial && !digitalRead(button_a_pin);
}
