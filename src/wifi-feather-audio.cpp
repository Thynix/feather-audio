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
#include <vector>

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
std::vector<const char*> filenames;

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
    Serial.println("Couldn't find seesaw on default address");
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
  //fileStack[0] = getStartingFile();
  populateFilenames(getStartingFile());
  filenames.shrink_to_fit();
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
  int adc = analogRead(volume_pin);

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
  //musicPlayer.setVolume(volume, volume);

  //Serial.printf("%d %f %d\n", adc, scaled_adc, volume);

  // Toggle pause on encoder button press.
  if (encoderButton.update(ss.digitalRead(SS_SWITCH)) && !encoderButton.get()) {
    paused = !paused;
    musicPlayer.pausePlaying(paused);

    if (paused) {
      sspixel.setPixelColor(0, 0xff0000);
    } else {
      sspixel.setPixelColor(0, 0x00ff00);
    }
    sspixel.show();
  } else {

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

        filename = filenames.back();
        filenames.pop_back();
        filenames.insert(filenames.begin(), filename);

        encoder_change++;
      } while (encoder_change < 0);
    } else if (encoder_change > 0) {
      do {
        Serial.println("Previous file");

        filename = filenames.front();
        filenames.erase(filenames.begin());
        filenames.push_back(filename);

        encoder_change--;
      } while (encoder_change > 0);
    }

    if (filename) {
      Serial.printf("Playing %s\n", filename);
      musicPlayer.stopPlaying();
      bool started = musicPlayer.startPlayingFile(filename);
      if (!started) {
        Serial.println("Failed");
      }
    }
  }
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
    Serial.printf("Listing %s from stack index %d\n", directory.name(), fileIndex);
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

      Serial.printf("%s exhausted\n", fileStack[fileIndex].name());
      fileIndex--;
      continue;
    }

    // Ignore System Volume Information
    if (!strcmp(entry.name(), "System Volume Information")) {
      entry.close();
      continue;
    }

    // If there's room to descend into a directory, do it.
    if (entry.isDirectory()) {
      if (fileIndex + 1 < fileStackSize) {
        Serial.printf("Descending into %s\n", entry.name());
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

void populateFilenames(File dir)
{
    while(true) {
      File entry =  dir.openNextFile();
      if (! entry) {
        // no more files
        //Serial.println("**nomorefiles**");
        break;
      }
      // Duplicate so the entry can be closed.
      char *name = (char*) malloc(strlen(entry.name()) + 1);
      strcpy(name, entry.name());
      filenames.push_back(name);

      // Recurse if necessary.
      if (entry.isDirectory()) {
        populateFilenames(entry);
      }

      entry.close();
  }
}
