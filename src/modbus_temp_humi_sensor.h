#ifndef MODBUS_TEMP_HUMI_SENSOR_H
#define MODBUS_TEMP_HUMI_SENSOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "tuya_iot.h"

void modbus_temp_humi_sensor_start(tuya_iot_client_t *client);

#ifdef __cplusplus
}
#endif

#endif // MODBUS_TEMP_HUMI_SENSOR_H