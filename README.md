# feather-audio

Interactive MP3 player for Adafruit Feather and PlatformIO.

Screen shows song tags, time elapsed, and playlist position. Songs are in
sorted into lexicographic order from the root of the MicroSD card. Uses an
encoder for song control, and potentiometer for volume.

## Hardware

* Encoder - https://www.adafruit.com/product/4991
* Potentiometer - https://www.adafruit.com/product/3391 or similar
* OLED 128x64 - https://www.adafruit.com/product/2900
* Feather M4 Express - https://www.adafruit.com/product/3857
* Music Maker FeatherWing - https://www.adafruit.com/product/3436 or https://www.adafruit.com/product/3357

Optional:

### Solar / Lithium Ion Polymer Charger - https://www.adafruit.com/product/4755

Easier portability between battery, solar power, and more easily accessible USB
port for power.

### Tactile button - https://www.adafruit.com/product/1119 or similar

Pulling pin 12 to ground stops playback and enters mass storage mode to offer
the MicroSD card over USB. This is extremely slow, but avoids the need to
remove the card and offers status information to make it more bearable.
