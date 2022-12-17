/*************************************************** 
  This is an example for the Adafruit VS1053

  Designed specifically to work with the Adafruit MusicMaker FeatherWing 
  ----> https://www.adafruit.com/product/3436

  Adafruit invests time and resources providing this open source code, 
  please support Adafruit and open-source hardware by purchasing 
  products from Adafruit!

  Written by Limor Fried/Ladyada for Adafruit Industries.  
  BSD license, all text above must be included in any redistribution
 ****************************************************/

// include SPI, MP3 and SD libraries
#include <SPI.h>
#include <Adafruit_VS1053.h>
#include <SD.h>
#include <Debouncer.h>
#include <vector>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_seesaw.h>
#include <seesaw_neopixel.h>

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

// Feather M4, M0, 328, ESP32S2, nRF52840 or 32u4
#else
  #define VS1053_CS       6     // VS1053 chip select pin (output)
  #define VS1053_DCS     10     // VS1053 Data/command select pin (output)
  #define CARDCS          5     // Card chip select pin
  // DREQ should be an Int pin *if possible* (not possible on 32u4)
  #define VS1053_DREQ     9     // VS1053 Data request, ideally an Interrupt pin
  #define NEOPIXEL_PIN    8

#endif

const uint8_t SS_SWITCH = 24;
const uint8_t SS_NEOPIX = 6;

const uint8_t SEESAW_ADDR = 0x36;

Adafruit_VS1053_FilePlayer musicPlayer = 
  Adafruit_VS1053_FilePlayer(VS1053_RESET, VS1053_CS, VS1053_DCS, VS1053_DREQ, CARDCS);
Adafruit_NeoPixel neopixel(1, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

Adafruit_seesaw ss;
seesaw_NeoPixel sspixel = seesaw_NeoPixel(1, SS_NEOPIX, NEO_GRB + NEO_KHZ800);

std::vector<String> files;

const uint8_t next_button_pin = 12;
const int debounce_duration_ms = 100;

int32_t encoder_position;

Debouncer next(next_button_pin, debounce_duration_ms);
Debouncer pause(debounce_duration_ms);

void setup() {
  Serial.begin(9600);

  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(next_button_pin, INPUT_PULLUP);

  neopixel.begin();
  neopixel.clear();
  neopixel.setBrightness(255);
  neopixel.show();
  
  // Music player
  if (!musicPlayer.begin() || !musicPlayer.useInterrupt(VS1053_FILEPLAYER_PIN_INT)) {
     Serial.println("couldn't find VS1053, or DREQ pin is not an interrupt pin");
     for (uint8_t i = 0; true; i++) {
        // Blink builtin LED every second
        if ((millis() / 1000) % 2) {
          digitalWrite(LED_BUILTIN, LOW);
        } else {
          digitalWrite(LED_BUILTIN, HIGH);
        }

        // Smoothly fade NeoPixel red
        neopixel.setPixelColor(0, i % 100, 0, 0);
        neopixel.show();

        delay(20);
     }
  }

#if 1
  // Beep while waiting for serial
  for (uint8_t i = 0; !Serial; i++) {
    if ((millis() / 1000) % 2) {
      musicPlayer.sineTest(0x42, 100); // 375 Hz
    }

    // Smoothly fade NeoPixel blue
    neopixel.setPixelColor(0, 0, 0, i % 100);
    neopixel.show();

    delay(20);
  }
#endif

  // Turn off NeoPixel after serial setup may have left it with a color.
  neopixel.clear();
  neopixel.show();

  Serial.println("startup");

  if (! ss.begin(SEESAW_ADDR) || ! sspixel.begin(SEESAW_ADDR)) {
    Serial.println("Couldn't find seesaw on default address");
    while(1) delay(10);
  }
  Serial.println("seesaw started");

  uint32_t version = ((ss.getVersion() >> 16) & 0xFFFF);
  if (version  != 4991){
    Serial.print("Wrong firmware loaded? ");
    Serial.println(version);
    while(1) delay(10);
  }

  // set not so bright!
  sspixel.setBrightness(0);
  sspixel.show();
  
  // use a pin for the built in encoder switch
  ss.pinMode(SS_SWITCH, INPUT_PULLUP);

  pause.setActiveState(Debouncer::Active::L);
  pause.stateFunc([](){
    return ss.digitalRead(SS_SWITCH);
  });

  // get starting position
  encoder_position = ss.getEncoderPosition();

  Serial.println("Turning on interrupts");
  delay(10);
  ss.setGPIOInterrupts(1u << SS_SWITCH, true);
  ss.enableEncoderInterrupt();

  // MicroSD card
  if (!SD.begin(CARDCS)) {
    Serial.println("SD failed, or not present");
    for (uint8_t i = 0; true; i++) {
      if ((millis() / 1000) % 2) {
        musicPlayer.sineTest(0x48, 200); // 1500 Hz
        musicPlayer.sineTest(0x43, 200); // 562.5 Hz
      }

      // Smoothly fade NeoPixel green
      neopixel.setPixelColor(0, 0, i, 0);
      neopixel.show();
    }
  }

  // Boot success chime
  musicPlayer.sineTest(0x42, 50); // 375 Hz
  musicPlayer.sineTest(0x44, 50); // 750 Hz

  // Load filenames
  auto root = SD.open("/");
  load(root);
  root.close();
  
  // Set volume for left, right channels. lower numbers == louder volume!
  auto volume = 1 - 0.7;
  const auto inaudible = 160;
  musicPlayer.setVolume(volume*inaudible, volume*inaudible);

  //incrementBootCount();

  Serial.println("Startup complete");
}

void loop() {
  auto paused = false;

  for (auto i = 0; i < files.size(); i++) {
    auto filename = files[i].c_str();

    next.update();
    pause.update();

    Serial.print(filename);
    musicPlayer.stopPlaying();
    musicPlayer.softReset();
    if (musicPlayer.startPlayingFile(filename)) {
      Serial.println(" started");

      while (musicPlayer.playingMusic || paused) {
        next.update();
        pause.update();

        if (next.falling()) {
          Serial.println("Next");
          break;
        }

        // falling()/rising() always return false for buttons with a state function.
        if (pause.edge() && !pause.read()) {
          paused = !paused;
          Serial.print("Pause ");
          Serial.println(paused);

          musicPlayer.pausePlaying(paused);
        } else if (!pause.edge()) {  // Ignore position changes when knob changes - liable to accidentally nudge.
          auto new_position = ss.getEncoderPosition();
          if (encoder_position != new_position) {
            auto difference = new_position - encoder_position;
            Serial.print("Moving ");
            Serial.println(difference);

            // -1 to account for increment on loop.
            i += difference - 1;

            // Wrap around playlist when negative.
            while (i < 0) {
              i += files.size();
            }

            // Wrap around playlist when beyond its length.
            i = i % files.size();

            encoder_position = new_position;
            break;
          }
        }
        delay(30);
      } 
    } else {
      Serial.println(" failed");
    }
  }
}

void load(File dir) {
   while (true) {
     auto entry =  dir.openNextFile();
     if (!entry) {
       return;
     }

    if (entry.isDirectory()) {
      entry.close();
      continue;
    }

    files.push_back(entry.name());
    
    entry.close();
   }
}

int incrementBootCount()
{
  const char* const logFilename = "boot-count.txt";
  long count = 0;

  auto log = SD.open(logFilename);
  if (!log) {
    Serial.println("No existing log file");
  } else {
    // Load existing value, if any.
    // If the file is empty, allocate something to read all the nothing in there.
    auto len = max(log.size(), 1);
    auto buf = malloc(len);
    if (!buf) {
      Serial.print("malloc() failed for len ");
      Serial.println(len);
      return logFailed();
    }
    
    memset(buf, 0, len);
    log.read(buf, len);

    // Returns 0 if invalid, in which case write 0 to it for real.
    // It might be new, or might be garbage.
    count = strtol((const char*) buf, NULL, 10);
    free(buf);
  }

  log.close();
  log = SD.open(logFilename, FILE_WRITE);

  log.println(count);
  Serial.print("Boot count ");
  Serial.println(count);

  log.close();
  log.flush();

  // Check that it actually shows up.
  if (!SD.open(logFilename))
    return logFailed();

  return 0;
}

int logFailed()
{
  // Failed to log boot, but continue booting anyway.
  musicPlayer.sineTest(0x43, 200); // 562.5 Hz
  musicPlayer.sineTest(0x42, 200); // lower

  return 1;
}
