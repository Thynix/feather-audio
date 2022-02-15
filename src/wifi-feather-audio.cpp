// Specifically for use with the Adafruit Feather, the pins are pre-set here!

// include SPI, MP3 and SD libraries
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <Adafruit_VS1053.h>
#include <patching.h>
#include <Debouncer.h>

// These are the pins used
#define VS1053_RESET   -1     // VS1053 reset pin (not used!)

// Feather ESP8266
#if defined(ESP8266)
  #define VS1053_CS      16     // VS1053 chip select pin (output)
  #define VS1053_DCS     15     // VS1053 Data/command select pin (output)
  #define CARDCS          2     // Card chip select pin
  #define VS1053_DREQ     0     // VS1053 Data request, ideally an Interrupt pin

// Feather ESP32
#elif defined(ESP32) && !defined(ARDUINO_ADAFRUIT_FEATHER_ESP32S2)
  #define VS1053_CS      32     // VS1053 chip select pin (output)
  #define VS1053_DCS     33     // VS1053 Data/command select pin (output)
  #define CARDCS         14     // Card chip select pin
  #define VS1053_DREQ    15     // VS1053 Data request, ideally an Interrupt pin

// Feather Teensy3
#elif defined(TEENSYDUINO)
  #define VS1053_CS       3     // VS1053 chip select pin (output)
  #define VS1053_DCS     10     // VS1053 Data/command select pin (output)
  #define CARDCS          8     // Card chip select pin
  #define VS1053_DREQ     4     // VS1053 Data request, ideally an Interrupt pin

// WICED feather
#elif defined(ARDUINO_STM32_FEATHER)
  #define VS1053_CS       PC7     // VS1053 chip select pin (output)
  #define VS1053_DCS      PB4     // VS1053 Data/command select pin (output)
  #define CARDCS          PC5     // Card chip select pin
  #define VS1053_DREQ     PA15    // VS1053 Data request, ideally an Interrupt pin

#elif defined(ARDUINO_NRF52832_FEATHER )
  #define VS1053_CS       30     // VS1053 chip select pin (output)
  #define VS1053_DCS      11     // VS1053 Data/command select pin (output)
  #define CARDCS          27     // Card chip select pin
  #define VS1053_DREQ     31     // VS1053 Data request, ideally an Interrupt pin

// Feather M4, M0, 328, ESP32-S2, nRF52840 or 32u4
#else
  #define VS1053_CS       6     // VS1053 chip select pin (output)
  #define VS1053_DCS     10     // VS1053 Data/command select pin (output)
  #define CARDCS          5     // Card chip select pin
  // DREQ should be an Int pin *if possible* (not possible on 32u4)
  #define VS1053_DREQ     9     // VS1053 Data request, ideally an Interrupt pin

#endif

void printDirectory(File dir, int numTabs);
File getNextFile();
File getStartingFile();

Adafruit_VS1053_FilePlayer musicPlayer = 
  Adafruit_VS1053_FilePlayer(VS1053_RESET, VS1053_CS, VS1053_DCS, VS1053_DREQ, CARDCS);

const uint8_t skip_pin = 14;
const int debounce_ms = 100;
Debouncer skipButton(debounce_ms);

// There's a lot of wobble in the volume knob reading; ignore changes less than this big.
const int adc_noise_threshold = 5;

const int fileStackSize = 5;
int fileIndex = 0;
File fileStack[fileStackSize] = {};

void setup() {
  Serial.begin(9600);

  // if you're using Bluefruit or LoRa/RFM Feather, disable the radio module
  //pinMode(8, INPUT_PULLUP);

  // Wait for serial port to be opened, remove this line for 'standalone' operation
  while (!Serial) { delay(10); }

  if (! musicPlayer.begin()) { // initialise the music player
     Serial.println(F("Couldn't find VS1053, do you have the right pins defined?"));
     while (1) delay(100);
  }

  Serial.println(F("VS1053 found")); 

  if (!SD.begin(CARDCS)) {
    Serial.println(F("SD failed, or not present"));
    while (1) delay(100);  // don't do anything more
  }
  Serial.println(F("SD OK!"));
  
  Serial.println(F("Patching VS1053"));
  musicPlayer.applyPatch(plugin, pluginSize);

  // list files
  //printDirectory(getStartingFile(), 0);
  fileStack[0] = getStartingFile();
  
  // Set volume for left, right channels. lower numbers == louder volume!
  musicPlayer.setVolume(255, 255);

#if defined(__AVR_ATmega32U4__) 
  // Timer interrupts are not suggested, better to use DREQ interrupt!
  // but we don't have them on the 32u4 feather...
  musicPlayer.useInterrupt(VS1053_FILEPLAYER_TIMER0_INT); // timer int
#else
  // If DREQ is on an interrupt pin we can do background
  // audio playing
  musicPlayer.useInterrupt(VS1053_FILEPLAYER_PIN_INT);  // DREQ int
#endif

  //pinMode(skip_pin, INPUT_PULLUP);
}

void loop() {
  int adc = analogRead(A0);

  // Voltage splitter
  // 330k / 100k Ohm ideal
  // 323k / 91k Ohm measured
  // 3.3v down to 0.725v
  // min / max seen with given potentiometer.
  const int adc_min = 20;
  const int adc_max = 800;
  float scaled_adc = (adc - adc_min) / (float) adc_max;
  if (scaled_adc < 0.0f) scaled_adc = 0.0f;
  if (scaled_adc > 1.0f) scaled_adc = 1.0f;

  // lower numbers == louder volume!
  // Because higher values are quieter, invert scaled ADC:
  // low ADC numbers should give high volume levels to be quiet.
  // 160 is low enough to seem silent.
  uint8_t volume = (uint8_t) ((1.0f - scaled_adc) * 160);

  // Set volume for left, right channels.
  musicPlayer.setVolume(volume, volume);

  Serial.printf("%d %f %d\n", adc, scaled_adc, volume);

  //bool button_changed = skipButton.update(digitalRead(skip_pin));

  // Go to the next file when done or when the skip button is pressed.
  if (musicPlayer.stopped()/* || (button_changed && !skipButton.get())*/) {
    Serial.println("Next file");

    do {
      File entry = getNextFile();
      Serial.printf("Playing %s\n", entry.fullName());
      bool started = musicPlayer.startPlayingFile(entry.fullName());
      entry.close();

      // Try the next one on failure to start.
      if (!started) {
        Serial.println("Playing failed");
        continue;
      }

    } while (!musicPlayer.playingMusic);
  }

  delay(100);
}

/// File listing helper
void printDirectory(File dir, int numTabs) {
   while(true) {
     File entry =  dir.openNextFile();
     if (! entry) {
       // no more files
       //Serial.println("**nomorefiles**");
       break;
     }
     for (uint8_t i=0; i<numTabs; i++) {
       Serial.print('\t');
     }
     Serial.print(entry.name());
     if (entry.isDirectory()) {
       Serial.println("/");
       printDirectory(entry, numTabs+1);
     } else {
       // files have sizes, directories do not
       Serial.print("\t\t");
       Serial.println(entry.size(), DEC);
     }
     entry.close();
   }
}

File getNextFile() {
  while (true) {
    File &directory = fileStack[fileIndex];
    Serial.printf("Listing %s from stack index %d\n", directory.fullName(), fileIndex);
    File entry = directory.openNextFile();

    // Top of stack exhausted; pop it.
    if (!entry) {
      directory.close();

      // Restart if at top level.
      if (fileIndex == 0) {
        Serial.println("Top exhausted; restarting");
        fileStack[0] = getStartingFile();
        continue;
      }

      Serial.printf("%s exhausted\n", fileStack[fileIndex].fullName());
      fileIndex--;
      continue;
    }

    // Ignore System Volume Information
    if (!strcmp(entry.fullName(), "System Volume Information")) {
      entry.close();
      continue;
    }

    // If there's room to descend into a directory, do it.
    if (entry.isDirectory()) {
      if (fileIndex + 1 < fileStackSize) {
        Serial.printf("Descending into %s\n", entry.fullName());
        fileStack[++fileIndex] = entry;
      }

      // Whether descending or not, a directory isn't a file, so skip it.
      continue;
    }

    return entry;
  }
}

File getStartingFile() {
  return SD.open("/");
}
