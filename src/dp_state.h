#ifndef DP_STATE_H
#define DP_STATE_H

#include <stdbool.h>
#include "tuya_iot_dp.h"

typedef struct {
    bool switch_1;
    bool auto_work;
    bool auto_work_direction;
    int current_humidity;
    int humi_high;
    int humi_low;
    float humi_offset;
    uint32_t sensor_status;
    int signal_strength;
} device_dp_values_t;

OPERATE_RET dp_state_init(void);
void dp_state_load(void);
void dp_state_get(device_dp_values_t *values);
float tuya_humidity_offset_get(void);
void dp_state_set_signal_strength(int signal_strength);
bool dp_state_update(const dp_obj_t *dp);
void dp_state_store_raw(const dp_raw_t *dp);

#endif
