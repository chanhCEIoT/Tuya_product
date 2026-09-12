#ifndef DP_REPORT_H
#define DP_REPORT_H

#include "tuya_iot_dp.h"

#ifdef __cplusplus
extern "C" {
#endif

OPERATE_RET dp_report_init(void);

OPERATE_RET dp_report_obj(tuya_iot_client_t *client,
                          const char *devid,
                          dp_obj_t *dps,
                          uint16_t dpscnt,
                          int flags);

OPERATE_RET dp_report_raw(tuya_iot_client_t *client,
                          const char *devid,
                          dp_raw_t *dp,
                          uint32_t timeout);

#ifdef __cplusplus
}
#endif

#endif
