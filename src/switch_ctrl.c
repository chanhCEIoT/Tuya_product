#include "switch_ctrl.h"
#include <stdio.h>
#include "tal_api.h"
#include "tkl_gpio.h"
#include "tuya_cloud_types.h"
#include "tuya_iot_dp.h"
#include "dp_report.h"
#include "status_led.h"
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
static bool auto_work = false;
static bool auto_work_direction = false;
static int humi_high = 0;
static int humi_low = 0;
static tuya_iot_client_t *switch_client = NULL;

void switch_ctrl_set_client(tuya_iot_client_t *client) {
    switch_client = client;
}

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
    bool changed = switch_state[channel] != on;
    switch_state[channel] = on;
    TUYA_GPIO_LEVEL_E level = on ? SWITCH_ACTIVE_LEVEL
                                 : ((SWITCH_ACTIVE_LEVEL == TUYA_GPIO_LEVEL_HIGH) ? TUYA_GPIO_LEVEL_LOW
                                                                                : TUYA_GPIO_LEVEL_HIGH);
    if (tkl_gpio_write(sg_switch_pins[channel], level) != OPRT_OK) {
        PR_DEBUG("Switch channel %u gpio write failed\n", channel);
    }

    if (channel == 0) {
        status_led_set_switch_state(on);
    }

    PR_DEBUG("Switch channel %d set to %s\n", channel, on ? "ON" : "OFF");

    if (changed && NULL != switch_client && tuya_iot_activated(switch_client) && tuya_iot_is_connected()) {
        dp_obj_t dp = {
            .id = 101,
            .type = PROP_BOOL,
            .value.dp_bool = on,
        };
        OPERATE_RET rt = dp_report_obj(
            switch_client,
            NULL,
            &dp,
            1,
            0
        );
        if (OPRT_OK != rt) {
            PR_ERR("Failed to report switch status, ret=%d", rt);
        }
    }
}

bool switch_ctrl_get(uint8_t channel) {
    if (channel >= SWITCH_MAX_CHANNELS) return false;
    return switch_state[channel];
}

void switch_ctrl_set_auto_config(bool enabled,
                                 bool direction,
                                 int high_threshold,
                                 int low_threshold) {
    auto_work = enabled;
    auto_work_direction = direction;
    humi_high = high_threshold;
    humi_low = low_threshold;
}

void switch_ctrl_auto_humidity(float humidity_percent) {
    if (!auto_work || humi_high <= humi_low) {
        return;
    }

    bool switch_on = switch_ctrl_get(0);
    if (humidity_percent >= (float)humi_high) {
        switch_on = auto_work_direction;
    } else if (humidity_percent <= (float)humi_low) {
        switch_on = !auto_work_direction;
    } else {
        return;
    }

    if (switch_on != switch_ctrl_get(0)) {
        switch_ctrl_set(0, switch_on);
        PR_INFO("Automatic humidity control: humidity=%.2f, switch=%d",
                humidity_percent, switch_on);
    }
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
