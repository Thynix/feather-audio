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

// Select only one to be true for SAMD21.
#define USING_TIMER_TC3         true      // Only TC3 can be used for SAMD51
#define USING_TIMER_TC4         false     // Do not use with Servo library
#define USING_TIMER_TC5         false
#define USING_TIMER_TCC         false
#define USING_TIMER_TCC1        false
#define USING_TIMER_TCC2        false     // Don't use this, can crash on some boards

#include "SAMDTimerInterrupt.h"

// TC3, TC4, TC5 max permissible TIMER_INTERVAL_MS is 1398.101 ms. Longer will
// overflow, and is therefore not permitted.
// Use TCC, TCC1, TCC2 for longer TIMER_INTERVAL_MS
#define TIMER_INTERVAL_MS        200

#if USING_TIMER_TC3
  #define SELECTED_TIMER      TIMER_TC3
#elif USING_TIMER_TC4
  #define SELECTED_TIMER      TIMER_TC4
#elif USING_TIMER_TC5
  #define SELECTED_TIMER      TIMER_TC5
#elif USING_TIMER_TCC
  #define SELECTED_TIMER      TIMER_TCC
#elif USING_TIMER_TCC1
  #define SELECTED_TIMER      TIMER_TCC1
#elif USING_TIMER_TCC2
  #define SELECTED_TIMER      TIMER_TCC
#else
  #error You have to select 1 Timer
#endif

// Init selected SAMD timer
SAMDTimer ITimer(SELECTED_TIMER);
volatile bool massStorageMode = false;

const char* const sd_card_mode = "SD card";
const int timer_init_error[] = {long_blink_ms, short_blink_ms, long_blink_ms, 0};

Adafruit_USBD_MSC usb_msc;

Sd2Card card;
SdVolume volume;

Debouncer massStorageButton(debounce_ms);

bool mass_storage_begin(uint8_t);
int32_t msc_read_cb(uint32_t, void*, uint32_t);
int32_t msc_write_cb(uint32_t, uint8_t*, uint32_t);
void msc_flush_cb();
void TimerHandler();

volatile unsigned int read_count = 0;
volatile unsigned int write_count = 0;

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

  if (ITimer.attachInterruptInterval_MS(TIMER_INTERVAL_MS, TimerHandler)) {
    Serial.println("ITimer set");
  } else {
    while (true) {
      Serial.println("Can't set mass storage button timer. Select another interval or timer.");
      led_blinkCode(timer_init_error);
    }
  }
}

bool mass_storage_button()
{
  return massStorageButton.update(digitalRead(mass_storage_pin)) && !massStorageButton.get();
}

void mass_storage_loop()
{
  Watchdog.disable();
  display_text(booting, sd_card_mode);

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

    for (uint8_t  j = 0; j < (i % 6); j++)
      strcpy(buf2 + strlen(buf2), ".");

    display_text(buf, buf2);
    delay(1000);
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

// Poll mass storage mode button on a timer.
void TimerHandler()
{
  massStorageMode = mass_storage_button();
}
