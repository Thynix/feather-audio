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
//#include <id3tag.h>

#define COUNT_OF(x) ((sizeof(x)/sizeof(0[x])) / ((size_t)(!(sizeof(x) % sizeof(0[x])))))

File file;

void populateFilenames(File);
void blinkCode(const int *);
float readVolume();

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

const char* const accepted_extensions[] = {
  ".MP3", ".mp3",
  ".OGG", ".ogg",
  ".FLA", ".fla",
};

#define SS_SWITCH        24
#define SS_NEOPIX        6

#define SEESAW_ADDR          0x36

Adafruit_seesaw ss;
seesaw_NeoPixel sspixel = seesaw_NeoPixel(1, SS_NEOPIX, NEO_GRB + NEO_KHZ800);

int32_t encoder_position;

const int short_blink_ms = 100;
const int long_blink_ms = 500;
const int after_pattern_ms = 1500;

const unsigned long target_frametime = 15;

const int no_seesaw[] = {long_blink_ms, short_blink_ms, short_blink_ms, long_blink_ms, 0};
const int wrong_seesaw[] = {long_blink_ms, long_blink_ms, short_blink_ms, long_blink_ms, 0};
const int no_VS1053[] = {short_blink_ms, short_blink_ms, long_blink_ms, long_blink_ms, 0};
const int no_microsd[] = {short_blink_ms, long_blink_ms, 0};

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

  // Blink while waiting for serial while button A is held.
  // Blink code: short on, short off
  while (!Serial && !digitalRead(button_a_pin)) {
    digitalWrite(LED_BUILTIN, LOW);
    delay(short_blink_ms);
    digitalWrite(LED_BUILTIN, HIGH);
    delay(short_blink_ms);
  }

  display_setup();

  Wire.begin();

  // Search for Seesaw device.
  // On failure, blink code: long off, short on, short off, long on
  if (! ss.begin(SEESAW_ADDR) || ! sspixel.begin(SEESAW_ADDR)) {
    display_song("cannot find", "encoder");
    while(true) blinkCode(no_seesaw);
  }

  // Check that found device is a rotary encoder.
  // On failure, blink code: long off, long on, short off, long on
  uint32_t version = ((ss.getVersion() >> 16) & 0xFFFF);
  if (version  != 4991){
    Serial.print("Wrong firmware loaded? Instead of rotary encoder, found product #");
    Serial.println(version);

    display_song("encoder", "version");

    while(true) blinkCode(wrong_seesaw);
  }

  // set not so bright!
  sspixel.setBrightness(10);
  sspixel.show();

  // use a pin for the built in encoder switch
  ss.pinMode(SS_SWITCH, INPUT_PULLUP);

  // get starting position
  encoder_position = ss.getEncoderPosition();

  delay(10);
  ss.setGPIOInterrupts((uint32_t)1 << SS_SWITCH, 1);
  ss.enableEncoderInterrupt();

  // Initialize music player. On failure, blink code: short off, short on, long off, long on
  if (!musicPlayer.begin()) {
    display_song("cannot find", "VS1053");

    while (true) blinkCode(no_VS1053);
  }

  // Initialize SD card. On failure, blink code: short off, long on
  if (!SD.begin(CARDCS)) {
    display_song("MicroSD failed", "or not present");

    while (true) blinkCode(no_microsd);
  }

  display_song("Patching", "VS1053");
  musicPlayer.applyPatch(plugin, pluginSize);

  display_song("Loading", "songs");
  File root = SD.open("/");
  populateFilenames(root);
  root.close();
  Serial.printf("Found %d songs\r\n", filenames.size());
  display_song("Loaded", "songs");

  if (filenames.size() == 0) {
    display_song("No songs", "found");
    while (true) blinkCode(no_microsd);
  }

  // DREQ is on an interrupt pin, so use background audio playing.
  musicPlayer.useInterrupt(VS1053_FILEPLAYER_PIN_INT);

  // Turn LED off now that startup is complete.
  digitalWrite(LED_BUILTIN, LOW);
}

void loop()
{
  static bool paused = false;
  static int previous_display_volume = -1;
  static int selected_file_index = -1;
  static unsigned long frame_times[60] = {};
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
  if (encoderButton.update(ss.digitalRead(SS_SWITCH)) && !encoderButton.get()) {
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
  } else if (!paused) {
    // The encoder position doesn't have a valid reading when the button is
    // pressed, so consider it only when the button isn't pressed.
    auto new_position = ss.getEncoderPosition();
    auto encoder_change = new_position - encoder_position;
    encoder_position = new_position;

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
      while (!musicPlayer.startPlayingFile(filenames[selected_file_index])) {
        Serial.println("Start failed");

        if (SD.begin(VS1053_CS)) {
          Serial.println("SD card reinitialized.");
        } else {
          Serial.println("Failed to reinitialize SD card.");
          display_song("SD reinit", "failed");
          delay(100);
        }

        display_song("SD reinit", "retry");
      }
    }
  }

  if (paused) {
    display_song(filenames[selected_file_index], "Paused");
  } else {
    char buf[32];
    sprintf(buf, "Volume %d", display_volume);
    display_song(filenames[selected_file_index], buf);
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
      if (entry.isDirectory()) {
        populateFilenames(entry);
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
  const int volumeReads = 20;

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
  // Blink (odd on, even off) until encountering 0 length terminator.
  for (int i = 0; delays[i]; i++) {
    if (i % 2) digitalWrite(LED_BUILTIN, HIGH);
    else       digitalWrite(LED_BUILTIN, LOW);

    delay(delays[i]);
  }

  digitalWrite(LED_BUILTIN, LOW);
  delay(after_pattern_ms);
}
