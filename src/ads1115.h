#ifndef ADS1115_H
#define ADS1115_H

#include <stdint.h>
#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

OPERATE_RET ads1115_init(void);
OPERATE_RET ads1115_read_adc0(int32_t *value);

#ifdef __cplusplus
}
#endif

#endif