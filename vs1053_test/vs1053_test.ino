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

#endif

Adafruit_VS1053_FilePlayer musicPlayer = 
  Adafruit_VS1053_FilePlayer(VS1053_RESET, VS1053_CS, VS1053_DCS, VS1053_DREQ, CARDCS);

std::vector<String> files;

const uint8_t next_button_pin = 12;
const int debounce_duration_ms = 50;

Debouncer next(next_button_pin, debounce_duration_ms, Debouncer::Active::L);

void setup() {
  Serial.begin(9600);
  Serial.println("Adafruit VS1053 Library Test");

  // initialise the music player
  if (! musicPlayer.begin()) { // initialise the music player
     Serial.println(F("Couldn't find VS1053, do you have the right pins defined?"));
     while (1);
  }
  Serial.println(F("VS1053 found"));

  if (!SD.begin(CARDCS)) {
    Serial.println(F("SD failed, or not present"));
    while (1);  // don't do anything more
  }
  Serial.println("SD OK!");

  musicPlayer.sineTest(0x44, 100);    // Make a tone to indicate VS1053 is working

  // Read button during startup so it doesn't immediately read as changed in loop()
  next.update();

  // list files
  load(SD.open("/"));

  for (auto filename : files) {
    Serial.println(filename);
  }
  
  // Set volume for left, right channels. lower numbers == louder volume!
  auto volume = 0.5f;
  musicPlayer.setVolume(volume*160, volume*160);

  /***** Two interrupt options! *******/ 
  // This option uses timer0, this means timer1 & t2 are not required
  // (so you can use 'em for Servos, etc) BUT millis() can lose time
  // since we're hitchhiking on top of the millis() tracker
  //musicPlayer.useInterrupt(VS1053_FILEPLAYER_TIMER0_INT);
  
  // This option uses a pin interrupt. No timers required! But DREQ
  // must be on an interrupt pin. For Uno/Duemilanove/Diecimilla
  // that's Digital #2 or #3
  // See http://arduino.cc/en/Reference/attachInterrupt for other pins
  // *** This method is preferred
  if (! musicPlayer.useInterrupt(VS1053_FILEPLAYER_PIN_INT))
    Serial.println(F("DREQ pin is not an interrupt pin"));
  
  next.update();

  Serial.println("Startup complete");
}

void loop() {
  for (int i = 0; i < files.size(); i++) {
    next.update();
    auto filename = files[i].c_str();

    Serial.print(filename);    
    if (musicPlayer.startPlayingFile(filename)) {
      Serial.println(" started");
    } else {
      Serial.println(" failed");
    }

    while (musicPlayer.playingMusic) {
      next.update();

      if (next.edge() && next.falling()) {
        Serial.println("Next button pressed");
        break;
      }

#if 0
      // previous
      if (digitalRead(9) == LOW) {
        // -2 so it goes to the next one on loop.
        i -= 2;

        // Wrap if before the start.
        if (i == -1)
          i = files.size() - 2;
        
        break;
      }

      // toggle pause
      if (digitalRead(6) == LOW) {
        static auto paused = false;
        paused = !paused;
        musicPlayer.pausePlaying(paused);        
      }

      // next
      if (digitalRead(5) == LOW) {
        break;
      }
#endif

      delay(20);
    }
  }
}


void load(File dir) {
   while(true) {
     File entry =  dir.openNextFile();
     if (!entry) {
       return;
     }

    if (entry.isDirectory()) {
      entry.close();
      continue;
    }

    files.push_back(String("/") + entry.name());
    
    entry.close();
   }
}

