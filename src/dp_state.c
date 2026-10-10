#include "dp_state.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "switch_ctrl.h"
#include "tal_api.h"

#define DP_STORAGE_PREFIX "dp_"
#define DP_DEFAULT_SWITCH_1 false
#define DP_DEFAULT_AUTO_WORK false
#define DP_DEFAULT_AUTO_DIRECTION false
#define DP_DEFAULT_CURRENT_HUMIDITY 0
#define DP_DEFAULT_HUMI_HIGH 80
#define DP_DEFAULT_HUMI_LOW 40
#define DP_DEFAULT_HUMI_OFFSET 0.0f
#define DP_DEFAULT_SENSOR_STATUS 0
#define DP_DEFAULT_SIGNAL_STRENGTH 0

static device_dp_values_t s_dp_values = {0};
static MUTEX_HANDLE s_dp_state_mutex = NULL;

OPERATE_RET dp_state_init(void)
{
    if (NULL != s_dp_state_mutex) {
        return OPRT_OK;
    }

    return tal_mutex_create_init(&s_dp_state_mutex);
}

static void dp_store_value(uint8_t id, int value)
{
    char key[sizeof(DP_STORAGE_PREFIX) + 4] = {0};
    char stored_value[16] = {0};

    snprintf(key, sizeof(key), DP_STORAGE_PREFIX "%u", id);
    snprintf(stored_value, sizeof(stored_value), "%d", value);
    if (OPRT_OK != tal_kv_set(key, (const uint8_t *)stored_value, strlen(stored_value))) {
        PR_ERR("Failed to store DP %u", id);
    }
}

static bool dp_load_value(uint8_t id, int *value)
{
    char key[sizeof(DP_STORAGE_PREFIX) + 4] = {0};
    uint8_t *stored_value = NULL;
    size_t stored_length = 0;

    snprintf(key, sizeof(key), DP_STORAGE_PREFIX "%u", id);
    if (OPRT_OK == tal_kv_get(key, &stored_value, &stored_length) && stored_length > 0) {
        stored_value[stored_length] = '\0';
        *value = atoi((char *)stored_value);
        tal_kv_free(stored_value);
        return true;
    }

    return false;
}

void dp_state_load(void)
{
    device_dp_values_t values = {
        .switch_1 = DP_DEFAULT_SWITCH_1,
        .auto_work = DP_DEFAULT_AUTO_WORK,
        .auto_work_direction = DP_DEFAULT_AUTO_DIRECTION,
        .current_humidity = DP_DEFAULT_CURRENT_HUMIDITY,
        .humi_high = DP_DEFAULT_HUMI_HIGH,
        .humi_low = DP_DEFAULT_HUMI_LOW,
        .humi_offset = DP_DEFAULT_HUMI_OFFSET,
        .sensor_status = DP_DEFAULT_SENSOR_STATUS,
        .signal_strength = DP_DEFAULT_SIGNAL_STRENGTH,
    };
    int value = 0;

    if (NULL == s_dp_state_mutex || OPRT_OK != tal_mutex_lock(s_dp_state_mutex)) {
        PR_ERR("Failed to lock DP state during load");
        return;
    }

    if (dp_load_value(101, &value)) {
        values.switch_1 = value != 0;
    }

    if (dp_load_value(111, &value)) {
        values.auto_work = value != 0;
    }

    if (dp_load_value(116, &value)) {
        values.auto_work_direction = value != 0;
    }

    dp_load_value(121, &values.current_humidity);
    dp_load_value(131, &values.humi_high);
    dp_load_value(132, &values.humi_low);

    if (dp_load_value(141, &value)) {
        values.humi_offset = (float)value / 10.0f;
    }

    if (dp_load_value(151, &value)) {
        values.sensor_status = (uint32_t)value;
    }

    dp_load_value(190, &values.signal_strength);

    s_dp_values = values;
    tal_mutex_unlock(s_dp_state_mutex);

    switch_ctrl_set(0, values.switch_1);
    switch_ctrl_set_auto_config(values.auto_work,
                                values.auto_work_direction,
                                values.humi_high,
                                values.humi_low);
}

void dp_state_get(device_dp_values_t *values)
{
    if (NULL == values || NULL == s_dp_state_mutex ||
        OPRT_OK != tal_mutex_lock(s_dp_state_mutex)) {
        return;
    }

    *values = s_dp_values;
    tal_mutex_unlock(s_dp_state_mutex);
}

float tuya_humidity_offset_get(void)
{
    float offset = 0.0f;

    if (NULL == s_dp_state_mutex || OPRT_OK != tal_mutex_lock(s_dp_state_mutex)) {
        return offset;
    }

    offset = s_dp_values.humi_offset;
    tal_mutex_unlock(s_dp_state_mutex);
    return offset;
}

void dp_state_set_signal_strength(int signal_strength)
{
    if (NULL == s_dp_state_mutex || OPRT_OK != tal_mutex_lock(s_dp_state_mutex)) {
        return;
    }

    if (s_dp_values.signal_strength == signal_strength) {
        tal_mutex_unlock(s_dp_state_mutex);
        return;
    }

    s_dp_values.signal_strength = signal_strength;
    tal_mutex_unlock(s_dp_state_mutex);
    dp_store_value(190, signal_strength);
}

bool dp_state_update(const dp_obj_t *dp)
{
    bool auto_config_changed = false;

    if (NULL == dp) {
        return false;
    }

    bool valid_type = false;
    switch (dp->id) {
    case 101:
    case 111:
    case 116:
        valid_type = PROP_BOOL == dp->type;
        break;
    case 121:
    case 131:
    case 132:
    case 141:
    case 190:
        valid_type = PROP_VALUE == dp->type;
        break;
    case 151:
        valid_type = PROP_ENUM == dp->type;
        break;
    default:
        return false;
    }
    if (!valid_type || NULL == s_dp_state_mutex ||
        OPRT_OK != tal_mutex_lock(s_dp_state_mutex)) {
        return false;
    }

    switch (dp->id) {
    case 101:
        s_dp_values.switch_1 = dp->value.dp_bool;
        dp_store_value(dp->id, dp->value.dp_bool);
        break;
    case 111:
        s_dp_values.auto_work = dp->value.dp_bool;
        dp_store_value(dp->id, dp->value.dp_bool);
        auto_config_changed = true;
        break;
    case 116:
        s_dp_values.auto_work_direction = dp->value.dp_bool;
        dp_store_value(dp->id, dp->value.dp_bool);
        auto_config_changed = true;
        break;
    case 121:
        s_dp_values.current_humidity = dp->value.dp_value;
        dp_store_value(dp->id, dp->value.dp_value);
        break;
    case 131:
        s_dp_values.humi_high = dp->value.dp_value;
        dp_store_value(dp->id, dp->value.dp_value);
        auto_config_changed = true;
        break;
    case 132:
        s_dp_values.humi_low = dp->value.dp_value;
        dp_store_value(dp->id, dp->value.dp_value);
        auto_config_changed = true;
        break;
    case 141:
        s_dp_values.humi_offset = (float)dp->value.dp_value / 10.0f;
        dp_store_value(dp->id, dp->value.dp_value);
        break;
    case 151:
        s_dp_values.sensor_status = dp->value.dp_enum;
        dp_store_value(dp->id, (int)dp->value.dp_enum);
        break;
    case 190:
        s_dp_values.signal_strength = dp->value.dp_value;
        dp_store_value(dp->id, dp->value.dp_value);
        break;
    default:
        tal_mutex_unlock(s_dp_state_mutex);
        return false;
    }

    device_dp_values_t values = s_dp_values;
    tal_mutex_unlock(s_dp_state_mutex);

    if (auto_config_changed) {
        switch_ctrl_set_auto_config(values.auto_work,
                                    values.auto_work_direction,
                                    values.humi_high,
                                    values.humi_low);
    }
    PR_INFO("DP %u value updated", dp->id);
    return true;
}

void dp_state_store_raw(const dp_raw_t *dp)
{
    char key[sizeof(DP_STORAGE_PREFIX) + 4] = {0};

    if (NULL == dp || 0 == dp->len) {
        return;
    }

    snprintf(key, sizeof(key), DP_STORAGE_PREFIX "%u", dp->id);
    if (OPRT_OK != tal_kv_set(key, dp->data, dp->len)) {
        PR_ERR("Failed to store raw DP %u", dp->id);
    }
}
