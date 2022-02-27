// Specifically for use with the Adafruit Feather, the pins are pre-set here!

// include SPI, MP3 and SD libraries
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
#include <list>

#define COUNT_OF(x) ((sizeof(x)/sizeof(0[x])) / ((size_t)(!(sizeof(x) % sizeof(0[x])))))

void printDirectory(File dir, int numTabs);
File getNextFile();
File getStartingFile();
void populateFilenames(File dir);

Adafruit_VS1053_FilePlayer musicPlayer = 
  Adafruit_VS1053_FilePlayer(VS1053_RESET, VS1053_CS, VS1053_DCS, VS1053_DREQ, CARDCS);

const int debounce_ms = 100;
Debouncer encoderButton(debounce_ms);

const uint8_t volume_pin = A2;

// There's a lot of wobble in the volume knob reading; ignore changes less than this big.
const int adc_noise_threshold = 5;

const int fileStackSize = 5;
int fileIndex = 0;
File fileStack[fileStackSize] = {};
std::list<const char*> filenames;

const char* const accepted_extensions[] = {
  ".MP3",
  ".OGG",
};

#define SS_SWITCH        24
#define SS_NEOPIX        6

#define SEESAW_ADDR          0x36

Adafruit_seesaw ss;
seesaw_NeoPixel sspixel = seesaw_NeoPixel(1, SS_NEOPIX, NEO_GRB + NEO_KHZ800);

int32_t encoder_position;

void setup()
{
  Serial.begin(9600);

  // if you're using Bluefruit or LoRa/RFM Feather, disable the radio module
  //pinMode(8, INPUT_PULLUP);

  pinMode(volume_pin, INPUT);

  pinMode(LED_BUILTIN, OUTPUT);

  // Blink while waiting for serial.
  while (!Serial) {
    digitalWrite(LED_BUILTIN, HIGH);
    delay(100);
    digitalWrite(LED_BUILTIN, LOW);
    delay(100);
  }

  Wire.begin();
 
  if (! ss.begin(SEESAW_ADDR) || ! sspixel.begin(SEESAW_ADDR)) {
    Serial.println("Couldn't find rotary encoder");
    while(true) delay(10);
  }

  uint32_t version = ((ss.getVersion() >> 16) & 0xFFFF);
  if (version  != 4991){
    Serial.print("Wrong firmware loaded? Instead of rotary encoder, found product #");
    Serial.println(version);
    while(true) delay(10);
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

  if (! musicPlayer.begin()) { // initialise the music player
     Serial.println(F("Couldn't find VS1053, do you have the right pins defined?"));
     while (1) delay(100);
  }

  if (!SD.begin(CARDCS)) {
    Serial.println(F("SD failed, or not present"));
    while (1) delay(100);  // don't do anything more
  }

  musicPlayer.applyPatch(plugin, pluginSize);

  populateFilenames(getStartingFile());
  Serial.printf("Found %d songs\n", filenames.size());
  
  // Set volume for left, right channels. lower numbers == louder volume!
  musicPlayer.setVolume(100, 100);

#if defined(__AVR_ATmega32U4__) 
  // Timer interrupts are not suggested, better to use DREQ interrupt!
  // but we don't have them on the 32u4 feather...
  musicPlayer.useInterrupt(VS1053_FILEPLAYER_TIMER0_INT); // timer int
#else
  // If DREQ is on an interrupt pin we can do background
  // audio playing
  musicPlayer.useInterrupt(VS1053_FILEPLAYER_PIN_INT);  // DREQ int
#endif
}

void loop()
{
  static bool paused = false;

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
      sspixel.setPixelColor(0, 0xff0000);
    } else {
      sspixel.setPixelColor(0, 0x000000);
    }
    sspixel.show();
  } else {
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
      do {
        Serial.println("Next file");

        filename = filenames.front();
        filenames.pop_front();
        filenames.push_back(filename);

        encoder_change++;
      } while (encoder_change < 0);
    } else if (encoder_change > 0) {
      do {
        Serial.println("Previous file");

        filename = filenames.back();
        filenames.pop_back();
        filenames.push_front(filename);

        encoder_change--;
      } while (encoder_change > 0);
    }

    if (filename) {
      Serial.printf("Playing %s\n", filename);
      /*
       * TODO: playing an MP3, then while that's playing trying to switch to an OGG has silent playback. When you try to play the OGG again it works.
       *       attempt to follow the datasheet in _datasheet_stopping broke stopping.
       */
      musicPlayer.stopPlaying();
      bool started = musicPlayer.startPlayingFile(filename);
      if (!started) {
        Serial.println("Failed");
      }

      // TODO: How to read tags to display song names?
    }
  }
}

File getStartingFile() {
  return SD.open("/");
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
      }

      for (size_t i = 0; i < COUNT_OF(accepted_extensions); i++) {
        if (strstr(entry.name(), accepted_extensions[i])) {
          goto accept_entry;
        }
      }

      // Ignore it if it doesn't have a usable extension.
      entry.close();
      continue;

accept_entry:
      // Duplicate so the entry can be closed.
      char *name = (char*) malloc(strlen(entry.name()) + 1);
      strcpy(name, entry.name());
      filenames.push_back(name);

      entry.close();
  }
}
