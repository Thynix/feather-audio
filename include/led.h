extern const int short_blink_ms;
extern const int long_blink_ms;
extern const int after_pattern_ms;

void led_init();
void led_blinkCode(const int *delays);
void led_on();
void led_off();
