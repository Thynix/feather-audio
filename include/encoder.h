#include <stdbool.h>

bool encoder_setup();
void encoder_loop();

void encoder_led_off();

bool encoder_togglePause();
int encoder_getChange();
