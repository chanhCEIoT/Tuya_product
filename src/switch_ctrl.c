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
static MUTEX_HANDLE switch_mutex = NULL;

void switch_ctrl_set_client(tuya_iot_client_t *client) {
    if (NULL == switch_mutex || OPRT_OK != tal_mutex_lock(switch_mutex)) {
        return;
    }
    switch_client = client;
    tal_mutex_unlock(switch_mutex);
}

void switch_ctrl_init(void) {
    OPERATE_RET rt = tal_mutex_create_init(&switch_mutex);
    if (OPRT_OK != rt) {
        PR_ERR("Failed to create switch mutex, ret=%d", rt);
        return;
    }

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
    if (channel >= SWITCH_MAX_CHANNELS || NULL == switch_mutex ||
        OPRT_OK != tal_mutex_lock(switch_mutex)) {
        return;
    }

    bool changed = switch_state[channel] != on;
    TUYA_GPIO_LEVEL_E level = on ? SWITCH_ACTIVE_LEVEL
                                 : ((SWITCH_ACTIVE_LEVEL == TUYA_GPIO_LEVEL_HIGH) ? TUYA_GPIO_LEVEL_LOW
                                                                                : TUYA_GPIO_LEVEL_HIGH);
    OPERATE_RET gpio_rt = tkl_gpio_write(sg_switch_pins[channel], level);
    if (OPRT_OK != gpio_rt) {
        PR_DEBUG("Switch channel %u gpio write failed\n", channel);
        tal_mutex_unlock(switch_mutex);
        return;
    }
    switch_state[channel] = on;

    if (channel == 0) {
        status_led_set_switch_state(on);
    }

    bool should_report = changed && NULL != switch_client;
    tuya_iot_client_t *client = switch_client;
    tal_mutex_unlock(switch_mutex);

    PR_DEBUG("Switch channel %d set to %s\n", channel, on ? "ON" : "OFF");

    if (should_report && tuya_iot_activated(client) && tuya_iot_is_connected()) {
        dp_obj_t dp = {
            .id = 101,
            .type = PROP_BOOL,
            .value.dp_bool = on,
        };

        OPERATE_RET rt = dp_report_obj(
            client,
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
    if (channel >= SWITCH_MAX_CHANNELS || NULL == switch_mutex ||
        OPRT_OK != tal_mutex_lock(switch_mutex)) {
        return false;
    }

    bool state = switch_state[channel];
    tal_mutex_unlock(switch_mutex);
    return state;
}

void switch_ctrl_set_auto_config(bool enabled,
                                 bool direction,
                                 int high_threshold,
                                 int low_threshold) {
    if (NULL == switch_mutex || OPRT_OK != tal_mutex_lock(switch_mutex)) {
        return;
    }

    auto_work = enabled;
    auto_work_direction = direction;
    humi_high = high_threshold;
    humi_low = low_threshold;
    tal_mutex_unlock(switch_mutex);
}

void switch_ctrl_auto_humidity(float humidity_percent) {
    if (NULL == switch_mutex || OPRT_OK != tal_mutex_lock(switch_mutex)) {
        return;
    }

    bool enabled = auto_work;
    bool direction = auto_work_direction;
    int high_threshold = humi_high;
    int low_threshold = humi_low;
    bool current_state = switch_state[0];
    tal_mutex_unlock(switch_mutex);

    if (!enabled || high_threshold <= low_threshold) {
        return;
    }

    bool switch_on = current_state;
    if (humidity_percent >= (float)high_threshold) {
        switch_on = direction;
    } else if (humidity_percent <= (float)low_threshold) {
        switch_on = !direction;
    } else {
        return;
    }

    if (switch_on != current_state) {
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
