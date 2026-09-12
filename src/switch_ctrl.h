#ifndef SWITCH_CTRL_H
#define SWITCH_CTRL_H

#include <stdbool.h>
#include <stdint.h>
#include "tuya_iot.h"

#ifdef __cplusplus
extern "C" {
#endif

void switch_ctrl_init(void);

void switch_ctrl_set_client(tuya_iot_client_t *client);

void switch_ctrl_set(uint8_t channel, bool on);

bool switch_ctrl_get(uint8_t channel);

void switch_ctrl_set_auto_config(bool auto_work,
								 bool auto_work_direction,
								 int humi_high,
								 int humi_low);

void switch_ctrl_auto_humidity(float humidity_percent);

void switch_ctrl_handle_dp(uint8_t dpid, bool value);

#ifdef __cplusplus
}
#endif

#endif // SWITCH_CTRL_H
