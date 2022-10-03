#include <Arduino.h>
#include <SPI.h>
#include <SdFat.h>
#include <Adafruit_VS1053.h>
#include <patching.h>
#include <Debouncer.h>
#include <Wire.h>
#include <Adafruit_seesaw.h>
#include <seesaw_neopixel.h>
// Specifically for use with the Adafruit M0 Feather, the pins are pre-set here!
#include <feather_pins.h>
#include <vector>

#define COUNT_OF(x) ((sizeof(x)/sizeof(0[x])) / ((size_t)(!(sizeof(x) % sizeof(0[x])))))

// SD_FAT_TYPE = 0 for SdFat/File as defined in SdFatConfig.h,
// 1 for FAT16/FAT32, 2 for exFAT, 3 for FAT16/FAT32 and exFAT.
#define SD_FAT_TYPE 3
//
// Set DISABLE_CHIP_SELECT to disable a second SPI device.
// For example, with the Ethernet shield, set DISABLE_CHIP_SELECT
// to 10 to disable the Ethernet controller.
const int8_t DISABLE_CHIP_SELECT = -1;
//
// Test with reduced SPI speed for breadboards.  SD_SCK_MHZ(4) will select
// the highest speed supported by the board that is not over 4 MHz.
// Change SPI_SPEED to SD_SCK_MHZ(50) for best performance.
#define SPI_SPEED SD_SCK_MHZ(4)
//------------------------------------------------------------------------------
#if SD_FAT_TYPE == 0
SdFat sd;
File file;
#elif SD_FAT_TYPE == 1
SdFat32 sd;
File32 file;
#elif SD_FAT_TYPE == 2
SdExFat sd;
ExFile file;
#elif SD_FAT_TYPE == 3
SdFs sd;
FsFile file;
#else  // SD_FAT_TYPE
#error Invalid SD_FAT_TYPE
#endif  // SD_FAT_TYPE

void populateFilenames(File);
void blinkCode(const int *);

Adafruit_VS1053_FilePlayer musicPlayer =
  Adafruit_VS1053_FilePlayer(VS1053_RESET, VS1053_CS, VS1053_DCS, VS1053_DREQ, CARDCS);

const int debounce_ms = 100;
Debouncer encoderButton(debounce_ms);

const uint8_t volume_pin = A2;
// Feather M4 button A
const uint8_t wait_for_serial_pin = 9;

// There's a lot of wobble in the volume knob reading; ignore changes less than this big.
const int adc_noise_threshold = 5;

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

const int no_seesaw[] = {long_blink_ms, short_blink_ms, short_blink_ms, long_blink_ms, 0};
const int wrong_seesaw[] = {long_blink_ms, long_blink_ms, short_blink_ms, long_blink_ms, 0};
const int no_VS1053[] = {short_blink_ms, short_blink_ms, long_blink_ms, long_blink_ms, 0};
const int no_microsd[] = {short_blink_ms, long_blink_ms, 0};

void setup()
{
  Serial.begin(9600);

  // if you're using Bluefruit or LoRa/RFM Feather, disable the radio module
  //pinMode(8, INPUT_PULLUP);

  pinMode(volume_pin, INPUT);
  pinMode(wait_for_serial_pin, INPUT_PULLUP);

  // Keep LED on during startup.
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  // Blink while waiting for serial while button A is held.
  // Blink code: short on, short off
  while (!Serial && !digitalRead(wait_for_serial_pin)) {
    digitalWrite(LED_BUILTIN, LOW);
    delay(100);
    digitalWrite(LED_BUILTIN, HIGH);
    delay(100);
  }

  Wire.begin();

  // Search for Seesaw device.
  // On failure, blink code: long off, short on, short off, long on
  if (! ss.begin(SEESAW_ADDR) || ! sspixel.begin(SEESAW_ADDR)) {
    Serial.println("Couldn't find rotary encoder");

    while(true) blinkCode(no_seesaw);
  }

  // Check that found device is a rotary encoder.
  // On failure, blink code: long off, long on, short off, long on
  uint32_t version = ((ss.getVersion() >> 16) & 0xFFFF);
  if (version  != 4991){
    Serial.print("Wrong firmware loaded? Instead of rotary encoder, found product #");
    Serial.println(version);

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
  if (! musicPlayer.begin()) {
    Serial.println(F("Couldn't find VS1053, do you have the right pins defined?"));

    while (true) blinkCode(no_VS1053);
  }

  // Initialize SD card. On failure, blink code: short off, long on
  if (!sd.begin(CARDCS, SPI_SPEED)) {
    Serial.println(F("MicroSD failed, or not present"));

    while (true) blinkCode(no_microsd);
  }

  musicPlayer.applyPatch(plugin, pluginSize);

  Serial.println(F("Searching for songs"));
  File root = sd.open("/");
  populateFilenames(root);
  root.close();
  Serial.printf("Found %d songs\r\n", filenames.size());

  if (filenames.size() == 0) {
    Serial.println(F("No songs found"));

    while (true) blinkCode(no_microsd);
  }

#if defined(__AVR_ATmega32U4__)
  // Timer interrupts are not suggested, better to use DREQ interrupt!
  // but we don't have them on the 32u4 feather...
  musicPlayer.useInterrupt(VS1053_FILEPLAYER_TIMER0_INT); // timer int
#else
  // If DREQ is on an interrupt pin we can do background
  // audio playing
  musicPlayer.useInterrupt(VS1053_FILEPLAYER_PIN_INT);  // DREQ int
#endif

  // Turn LED off now that startup is complete.
  digitalWrite(LED_BUILTIN, LOW);
}

void loop()
{
  static bool paused = false;
  static int file_index = 0;

  // When using a linear potentiometer, take the log to match volume perception.
  // ADC max is 1023, and natural log of 1023 = 6.93049
  // No need if using an audio potentiometer of course.
  float scaled_adc = log(max(analogRead(volume_pin), 1u)) / 6.93049;
  // float scaled_adc = analogRead(volume_pin) / 1023.0;
  if (scaled_adc < 0.0f) scaled_adc = 0.0f;
  if (scaled_adc > 1.0f) scaled_adc = 1.0f;

  // lower numbers == louder volume!
  // Because higher values are quieter, invert scaled ADC:
  // low ADC numbers should give high volume levels to be quiet.
  // 160 is low enough to seem silent.
  uint8_t volume = (uint8_t) ((1.0f - scaled_adc) * 160);

  // Set volume for left, right channels.
  musicPlayer.setVolume(volume, volume);

  //Serial.println(volume);

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
    const char* filename = NULL;
    if (musicPlayer.stopped() || encoder_change < 0) {
      Serial.print("Next ");
      do {
        filename = filenames[file_index];
        file_index++;
        if ((size_t) file_index >= filenames.size()) {
          file_index = 0;
        }

        Serial.print(encoder_change);
        Serial.print(' ');

        encoder_change++;
      } while (encoder_change < 0);
      Serial.println();
    } else if (encoder_change > 0) {
      Serial.print("Previous ");
      do {
        filename = filenames[file_index];
        file_index--;
        if (file_index < 0) {
          file_index = filenames.size() - 1;
        }

        Serial.print(encoder_change);
        Serial.print(' ');

        encoder_change--;
      } while (encoder_change > 0);
      Serial.println();
    }

    if (filename) {
      if (!file.open(filename)) {
        uint8_t errorCode = file.getError();
        Serial.printf("Opening \"%s\" failed (%#0x - ", filename, (int) errorCode);
        printSdErrorSymbol(&Serial, errorCode);
        Serial.print(" - ");
        printSdErrorText(&Serial, errorCode);
        Serial.println(")");
      } else {
        Serial.printf("Playing %s\r\n", filename);
      }

      file.close();

      /*
       * TODO: playing an MP3, then while that's playing trying to switch to an OGG has silent playback. When you try to play the OGG again it works.
       *       attempt to follow the datasheet in _datasheet_stopping broke stopping.
       */
      musicPlayer.stopPlaying();
      while (!musicPlayer.startPlayingFile(filename, &sd)) {
        Serial.println("Start failed");

        sd.end();
        if (sd.begin(CARDCS, SPI_SPEED)) {
          Serial.println("SD card reinitialized.");
        } else {
          Serial.println("Failed to reinitialize SD card.");
          Serial.println(sd.card()->errorCode());
          Serial.println(sd.card()->errorData());
        }
      }
    }
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

      char nameBuf[33] = {};
      entry.getName(nameBuf, sizeof(nameBuf));

      /*
      Serial.print("Found \"");
      Serial.print(nameBuf);
      Serial.println("\"");
      */

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
