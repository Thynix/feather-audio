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
#include <Debouncer.h>
#include <SD.h>

// Feather ESP32
#if defined(ESP32) && !defined(ARDUINO_ADAFRUIT_FEATHER_ESP32S2)

// TODO

// Feather M4, M0, 328, ESP32-S2, nRF52840 or 32u4
#else

const uint8_t mass_storage_pin = 12;

#endif

const char* const mass_storage_mode = "Mass storage mode";


Adafruit_USBD_MSC usb_msc;

Sd2Card card;
SdVolume volume;

Debouncer massStorageButton(debounce_ms);

bool mass_storage_begin(uint8_t);
int32_t msc_read_cb(uint32_t, void*, uint32_t);
int32_t msc_write_cb(uint32_t, uint8_t*, uint32_t);
void msc_flush_cb();

volatile bool mass_storage_got_read;

void mass_storage_setup()
{
  pinMode(mass_storage_pin, INPUT_PULLUP);

  // Set disk vendor id, product id and revision with string up to 8, 16, 4 characters respectively
  usb_msc.setID("Steve", "MP3 Player", "1.0");

  // Set read write callback
  usb_msc.setReadWriteCallback(msc_read_cb, msc_write_cb, msc_flush_cb);

  // Still initialize MSC but tell usb stack that MSC is not ready to read/write
  // If we don't initialize, board will be enumerated as CDC only
  usb_msc.setUnitReady(false);
  usb_msc.begin();

  mass_storage_got_read = false;
}

bool mass_storage_button()
{
  return massStorageButton.update(digitalRead(mass_storage_pin)) && !massStorageButton.get();
}

void mass_storage_loop()
{
  Watchdog.disable();
  display_text(mass_storage_mode, booting);

  if (!mass_storage_begin(CARDCS)) {
    display_text("Mass storage failed", boot_error);

    while (true) led_blinkCode(no_microsd);
  }

  while (true) {
    display_text(mass_storage_mode, mass_storage_got_read ? "Got reads" : "No reads yet");
    delay(1000);
  }
}

bool mass_storage_begin(uint8_t chipSelectPin)
{
  if (!card.init(SPI_HALF_SPEED, chipSelectPin))
  {
    Serial.println("initialization failed. Things to check:");
    Serial.println("* is a card inserted?");
    Serial.println("* is your wiring correct?");
    Serial.println("* did you change the chipSelect pin to match your shield or module?");
    return false;
  }

  // Now we will try to open the 'volume'/'partition' - it should be FAT16 or FAT32
  if (!volume.init(card)) {
    Serial.println("Could not find FAT16/FAT32 partition.\nMake sure you've formatted the card");
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
  mass_storage_got_read = true;
  (void) bufsize;
  return card.readBlock(lba, (uint8_t*) buffer) ? 512 : -1;
}

// Callback invoked when received WRITE10 command.
// Process data in buffer to disk's storage and
// return number of written bytes (must be multiple of block size)
int32_t msc_write_cb(long unsigned int lba, unsigned char *buffer, long unsigned int bufsize)
{
  (void) bufsize;
  return card.writeBlock(lba, buffer) ? 512 : -1;
}

// Callback invoked when WRITE10 command is completed (status received and accepted by host).
// used to flush any pending cache.
void msc_flush_cb()
{
  // nothing to do
}
