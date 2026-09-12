#include "dp_report.h"

#include "tal_api.h"

static MUTEX_HANDLE s_dp_report_mutex = NULL;

OPERATE_RET dp_report_init(void)
{
    if (NULL != s_dp_report_mutex) {
        return OPRT_OK;
    }

    return tal_mutex_create_init(&s_dp_report_mutex);
}

OPERATE_RET dp_report_obj(tuya_iot_client_t *client,
                          const char *devid,
                          dp_obj_t *dps,
                          uint16_t dpscnt,
                          int flags)
{
    if (NULL == client || !tuya_iot_is_connected()) {
        return OPRT_COM_ERROR;
    }

    if (NULL == s_dp_report_mutex) {
        return OPRT_COM_ERROR;
    }

    if (OPRT_OK != tal_mutex_lock(s_dp_report_mutex)) {
        return OPRT_COM_ERROR;
    }
    OPERATE_RET rt = tuya_iot_dp_obj_report(client, devid, dps, dpscnt, flags);
    tal_mutex_unlock(s_dp_report_mutex);
    return rt;
}

OPERATE_RET dp_report_raw(tuya_iot_client_t *client,
                          const char *devid,
                          dp_raw_t *dp,
                          uint32_t timeout)
{
    if (NULL == client || !tuya_iot_is_connected()) {
        return OPRT_COM_ERROR;
    }

    if (NULL == s_dp_report_mutex) {
        return OPRT_COM_ERROR;
    }

    if (OPRT_OK != tal_mutex_lock(s_dp_report_mutex)) {
        return OPRT_COM_ERROR;
    }
    OPERATE_RET rt = tuya_iot_dp_raw_report(client, devid, dp, timeout);
    tal_mutex_unlock(s_dp_report_mutex);
    return rt;
}
