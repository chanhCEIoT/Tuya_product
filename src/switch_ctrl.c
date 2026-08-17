#include "switch_ctrl.h"
#include <stdio.h>
#include "tal_api.h"
#include "tkl_gpio.h"
#include "tuya_cloud_types.h"
#define SWITCH_MAX_CHANNELS 1

#ifndef SWITCH_GPIO_CH1
#define SWITCH_GPIO_CH1 TUYA_GPIO_NUM_16
#endif

#ifndef SWITCH_ACTIVE_LEVEL
#define SWITCH_ACTIVE_LEVEL TUYA_GPIO_LEVEL_HIGH
#endif

static const TUYA_GPIO_NUM_E sg_switch_pins[SWITCH_MAX_CHANNELS] = {
    SWITCH_GPIO_CH1,
};

static bool switch_state[SWITCH_MAX_CHANNELS] = {0};

void switch_ctrl_init(void) {
    OPERATE_RET rt = OPRT_OK;
    TUYA_GPIO_LEVEL_E off_level =
        (SWITCH_ACTIVE_LEVEL == TUYA_GPIO_LEVEL_HIGH) ? TUYA_GPIO_LEVEL_LOW : TUYA_GPIO_LEVEL_HIGH;

    TUYA_GPIO_BASE_CFG_T out_pin_cfg = {
        .mode = TUYA_GPIO_PUSH_PULL, .direct = TUYA_GPIO_OUTPUT, .level = off_level};
    
    for (int i = 0; i < SWITCH_MAX_CHANNELS; ++i) {
        switch_state[i] = false;
        TUYA_CALL_ERR_LOG(tkl_gpio_init(sg_switch_pins[i], &out_pin_cfg));
    }
}

void switch_ctrl_set(uint8_t channel, bool on) {
    if (channel >= SWITCH_MAX_CHANNELS) return;
    switch_state[channel] = on;
    TUYA_GPIO_LEVEL_E level = on ? SWITCH_ACTIVE_LEVEL
                                 : ((SWITCH_ACTIVE_LEVEL == TUYA_GPIO_LEVEL_HIGH) ? TUYA_GPIO_LEVEL_LOW
                                                                                : TUYA_GPIO_LEVEL_HIGH);
    if (tkl_gpio_write(sg_switch_pins[channel], level) != OPRT_OK) {
        PR_DEBUG("Switch channel %u gpio write failed\n", channel);
    }

    PR_DEBUG("Switch channel %d set to %s\n", channel, on ? "ON" : "OFF");
}

bool switch_ctrl_get(uint8_t channel) {
    if (channel >= SWITCH_MAX_CHANNELS) return false;
    return switch_state[channel];
}

void switch_ctrl_handle_dp(uint8_t dpid, bool value) {
    int channel = -1;

    PR_DEBUG("Received DP ID: %u, value: %d", dpid, value);

    switch (dpid) {
    case 101:
        channel = 0;
        break;
    default:
        PR_WARN("Unhandled DP ID: %u", dpid);
        return;
    }

    switch_ctrl_set((uint8_t)channel, value);
}
