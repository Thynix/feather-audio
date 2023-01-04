/*********************************************************************
 Adafruit invests time and resources providing this open source code,
 please support Adafruit and open-source hardware by purchasing
 products from Adafruit!

 MIT license, check LICENSE for more information
 Copyright (c) 2019 Ha Thach for Adafruit Industries
 All text above, and the splash screen below must be included in
 any redistribution
*********************************************************************/

#include <mass_storage.h>

#include <constants.h>
#include <display.h>
#include <vs1053.h>

#include <Adafruit_SleepyDog.h>
#include <Adafruit_TinyUSB.h>
#include <SD.h>
#include <Switch.h>

// Feather ESP32
#if defined(ESP32) && !defined(ARDUINO_ADAFRUIT_FEATHER_ESP32S2)

// TODO

// Feather M4, M0, 328, ESP32-S2, nRF52840 or 32u4
#else

const uint8_t massStorageButtonPin = 4;
const uint8_t softwareResetPin =     A3;

#endif

const char* const sd_card_mode = "Card";

Adafruit_USBD_MSC usb_msc;

Sd2Card card;
SdVolume volume;

Switch massStorageButton(massStorageButtonPin);

bool mass_storage_begin(uint8_t);
int32_t msc_read_cb(uint32_t, void*, uint32_t);
int32_t msc_write_cb(uint32_t, uint8_t*, uint32_t);
void msc_flush_cb();
void TimerHandler();

volatile unsigned int read_count = 0;
volatile unsigned int write_count = 0;

void mass_storage_setup()
{
  digitalWrite(softwareResetPin, HIGH);
  pinMode(softwareResetPin, OUTPUT);

  // Set disk vendor id, product id and revision with string up to 8, 16, 4 characters respectively
  usb_msc.setID("Steve", "MP3 Player", "1.0");

  // Set read write callback
  usb_msc.setReadWriteCallback(msc_read_cb, msc_write_cb, msc_flush_cb);

  // Still initialize MSC but tell usb stack that MSC is not ready to read/write
  // If we don't initialize, board will be enumerated as CDC only
  usb_msc.setUnitReady(false);
  usb_msc.begin();
}

bool mass_storage_button()
{
  static int buttonPresses;

  massStorageButton.poll();

  if (massStorageButton.pushed()) {
    buttonPresses++;

    Serial.println("Mass storage button pressed");

    // Second press: reset to reload songs.
    if (buttonPresses != 1)
      digitalWrite(softwareResetPin, LOW);

    // First press: instruct main() to start mass storage mode.
    return true;
  }

  return false;
}

void mass_storage_mode()
{
  Watchdog.disable();

  display_text(booting, sd_card_mode);

  // Regenerate the cache on next startup as the card may have been modified.
  vs1053_clearSongCache();

  if (!mass_storage_begin(CARDCS)) {
    display_text("Mass storage failed", boot_error);

    while (true) led_blinkCode(no_microsd);
  }

  // Show signs of life to make the wait more bearable.
  char buf[32];
  char buf2[32];
  for (uint8_t i = 0; ; i++) {
    sprintf(buf, "R%u W%u", read_count, write_count);
    strcpy(buf2, sd_card_mode);

    for (uint8_t j = 0; j < (i % 6); j++)
      strcpy(buf2 + strlen(buf2), ".");

    display_text(buf, buf2);
    for (uint8_t j = 0; j < 100; j++) {
      mass_storage_button();
      delay(10);
    }
  }
}

bool mass_storage_begin(uint8_t chipSelectPin)
{
  if (!card.init(SPI_FULL_SPEED, chipSelectPin))
  {
    Serial.println("initialization failed. Things to check:");
    Serial.println("* is a card inserted?");
    Serial.println("* is your wiring correct?");
    Serial.println("* did you change the chipSelect pin to match your shield or module?");
    return false;
  }

  // Now we will try to open the 'volume'/'partition' - it should be FAT16 or FAT32
  if (!volume.init(card)) {
    Serial.println("Could not find FAT16/FAT32 partition\r\nMake sure you've formatted the card");
    return false;
  }

  uint32_t block_count = volume.blocksPerCluster()*volume.clusterCount();

  Serial.print("Volume size (MB):  ");
  Serial.println((block_count/2) / 1024);

  // Set disk size, SD block size is always 512
  usb_msc.setCapacity(block_count, 512);

  // MSC is ready for read/write
  usb_msc.setUnitReady(true);

  return true;
}

// Callback invoked when received READ10 command.
// Copy disk's data to buffer (up to bufsize) and
// return number of copied bytes (must be multiple of block size)
int32_t msc_read_cb(uint32_t lba, void* buffer, uint32_t bufsize)
{
  read_count++;
  (void) bufsize;
  return card.readBlock(lba, (uint8_t*) buffer) ? 512 : -1;
}

// Callback invoked when received WRITE10 command.
// Process data in buffer to disk's storage and
// return number of written bytes (must be multiple of block size)
int32_t msc_write_cb(long unsigned int lba, unsigned char *buffer, long unsigned int bufsize)
{
  write_count++;
  (void) bufsize;
  return card.writeBlock(lba, buffer) ? 512 : -1;
}

// Callback invoked when WRITE10 command is completed (status received and accepted by host).
// used to flush any pending cache.
void msc_flush_cb()
{
  // nothing to do
}
