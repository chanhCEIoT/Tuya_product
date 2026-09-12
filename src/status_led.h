#ifndef STATUS_LED_H
#define STATUS_LED_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void status_led_init(void);
void status_led_set_pairing(bool pairing);
void status_led_set_paired(bool paired);
void status_led_set_switch_state(bool switch_on);
void status_led_update(void);

#ifdef __cplusplus
}
#endif

#endif /* STATUS_LED_H */