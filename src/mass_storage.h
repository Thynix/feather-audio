#include <stdint.h>

// Initialize mass storage. Call this first.
void mass_storage_init();

// Set up mass storage. Call this second. Writes to Serial, which it assumes is ready.
bool mass_storage_begin(uint8_t chipSelectPin);

extern volatile bool mass_storage_got_read;
