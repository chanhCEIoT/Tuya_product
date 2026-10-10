/**
 * @file tuya_main.c
 * @brief tuya_main module is used to manage the Tuya device application.
 *
 * This file provides the implementation of the tuya_main module,
 * which is responsible for managing the Tuya device application.
 *
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 *
 * 2025-07-11   yangjie     Add cellular network support.
 */

#include "cJSON.h"
#include "netmgr.h"
#include "tal_api.h"
#include "tal_wifi.h"
#include "tkl_output.h"
#include "tuya_config.h"
#include "tuya_iot.h"
#include "tuya_iot_dp.h"
#include "tal_cli.h"
#include "tuya_authorize.h"
#include "switch_ctrl.h"
#if defined(ENABLE_WIFI) && (ENABLE_WIFI == 1)
#include "netconn_wifi.h"
#endif
#if defined(ENABLE_WIRED) && (ENABLE_WIRED == 1)
#include "netconn_wired.h"
#endif
#if defined(ENABLE_CELLULAR) && (ENABLE_CELLULAR == 1)
#include "netconn_cellular.h"
#endif
#if defined(ENABLE_LIBLWIP) && (ENABLE_LIBLWIP == 1)
#include "lwip_init.h"
#endif

#include "reset_netcfg.h"
#include "humi_sensor.h"
#include "dp_report.h"
#include "dp_state.h"
#include "status_led.h"

#if defined(ENABLE_QRCODE) && (ENABLE_QRCODE == 1)
#include "qrencode_print.h"
#endif

#ifndef PROJECT_VERSION
#define PROJECT_VERSION "1.0.0"
#endif


/* Tuya device handle */
tuya_iot_client_t client;
tuya_iot_license_t license;

static THREAD_HANDLE s_dp_restore_thread = NULL;
static THREAD_HANDLE s_wifi_signal_thread = NULL;
#define DP_RAW_STORAGE_PREFIX "dp_"

static OPERATE_RET dp_report_all(void)
{
    device_dp_values_t dp_values = {0};
    dp_state_get(&dp_values);
    dp_obj_t dps[] = {
        {.id = 101, .type = PROP_BOOL, .value.dp_bool = dp_values.switch_1},
        {.id = 111, .type = PROP_BOOL, .value.dp_bool = dp_values.auto_work},
        {.id = 116, .type = PROP_BOOL, .value.dp_bool = dp_values.auto_work_direction},
        // {.id = 121, .type = PROP_VALUE, .value.dp_value = dp_values->current_humidity},
        {.id = 131, .type = PROP_VALUE, .value.dp_value = dp_values.humi_high},
        {.id = 132, .type = PROP_VALUE, .value.dp_value = dp_values.humi_low},
        {.id = 141, .type = PROP_VALUE, .value.dp_value = (int)(dp_values.humi_offset * 10.0f)},
        // {.id = 151, .type = PROP_ENUM, .value.dp_enum = dp_values->sensor_status},
        // {.id = 190, .type = PROP_VALUE, .value.dp_value = dp_values->signal_strength},
    };

    return dp_report_obj(&client, NULL, dps, sizeof(dps) / sizeof(dps[0]), 0);
}

static int wifi_signal_strength_get(void)
{
    int8_t rssi = 0;

    if (OPRT_OK != tal_wifi_station_get_conn_ap_rssi(&rssi)) {
        return 0;
    }

    if (rssi <= -100) {
        return 1;
    }
    if (rssi >= -30) {
        return 100;
    }

    return 1 + ((int)rssi + 100) * 99 / 70;
}

static OPERATE_RET wifi_signal_strength_report(void)
{
    int signal_strength = wifi_signal_strength_get();
    if (signal_strength == 0) {
        return OPRT_COM_ERROR;
    }

    dp_state_set_signal_strength(signal_strength);

    dp_obj_t dp = {
        .id = 190,
        .type = PROP_VALUE,
        .value.dp_value = signal_strength,
    };

    return dp_report_obj(&client, NULL, &dp, 1, 0);
}

static void wifi_signal_task(void *arg)
{
    (void)arg;

    while (1) {
        if (tuya_iot_is_connected()) {
            OPERATE_RET rt = wifi_signal_strength_report();
            if (OPRT_OK != rt) {
                PR_DEBUG("WiFi signal report failed, ret=%d", rt);
            }
        }
        tal_system_sleep(2000);
    }
}

static void wifi_signal_start(void)
{
    THREAD_CFG_T thread_cfg = {
        .stackDepth = 1024 * 4,
        .priority = THREAD_PRIO_1,
        .thrdname = "wifi_signal_task",
    };

    OPERATE_RET rt = tal_thread_create_and_start(
        &s_wifi_signal_thread,
        NULL,
        NULL,
        wifi_signal_task,
        NULL,
        &thread_cfg
    );
    if (OPRT_OK != rt) {
        PR_ERR("Failed to create WiFi signal task, ret=%d", rt);
        s_wifi_signal_thread = NULL;
    }
}

static OPERATE_RET dp_report_stored_raw(void)
{
    char key[sizeof(DP_RAW_STORAGE_PREFIX) + 4] = {0};
    uint8_t *stored_value = NULL;
    size_t stored_length = 0;

    snprintf(key, sizeof(key), DP_RAW_STORAGE_PREFIX "160");
    OPERATE_RET rt = tal_kv_get(key, &stored_value, &stored_length);
    if (OPRT_OK != rt || NULL == stored_value || 0U == stored_length) {
        if (NULL != stored_value) {
            tal_kv_free(stored_value);
        }
        return OPRT_OK;
    }

    if (stored_length > UINT16_MAX || stored_length > SIZE_MAX - sizeof(dp_raw_t)) {
        tal_kv_free(stored_value);
        return OPRT_BUFFER_NOT_ENOUGH;
    }

    dp_raw_t *dp = tal_malloc(sizeof(dp_raw_t) + stored_length);
    if (NULL == dp) {
        tal_kv_free(stored_value);
        return OPRT_MALLOC_FAILED;
    }

    dp->id = 160;
    dp->len = (uint16_t)stored_length;
    memcpy(dp->data, stored_value, stored_length);
    tal_kv_free(stored_value);

    rt = dp_report_raw(&client, NULL, dp, 5000);
    tal_free(dp);
    return rt;
}

static void dp_restore_task(void *arg)
{
    (void)arg;

    dp_state_load();
    while (!tuya_iot_is_connected()) {
        tal_system_sleep(2000);
    }

    OPERATE_RET rt = dp_report_all();
    if (OPRT_OK != rt) {
        PR_ERR("Failed to report restored DPs, ret=%d", rt);
    }
    rt = dp_report_stored_raw();
    if (OPRT_OK != rt) {
        PR_ERR("Failed to report restored raw DPs, ret=%d", rt);
    }

    THREAD_HANDLE thread = s_dp_restore_thread;
    s_dp_restore_thread = NULL;
    tal_thread_delete(thread);
}

static void dp_restore_start(void)
{
    if (NULL != s_dp_restore_thread) {
        return;
    }

    THREAD_CFG_T thread_cfg = {
        .stackDepth = 1024 * 4,
        .priority = THREAD_PRIO_1,
        .thrdname = "dp_restore_task",
    };

    OPERATE_RET rt = tal_thread_create_and_start(
        &s_dp_restore_thread,
        NULL,
        NULL,
        dp_restore_task,
        NULL,
        &thread_cfg
    );
    if (OPRT_OK != rt) {
        PR_ERR("Failed to create DP restore task, ret=%d", rt);
        s_dp_restore_thread = NULL;
    }
}

void user_log_output_cb(const char *str)
{
    tkl_log_output(str);
}

static const char *upgrade_string(cJSON *upgrade, const char *key)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(upgrade, key);
    if (NULL == item || !cJSON_IsString(item) || NULL == item->valuestring) {
        return "<missing>";
    }

    return item->valuestring;
}

void user_upgrade_notify_on(tuya_iot_client_t *client, cJSON *upgrade)
{
    (void)client;
    if (NULL == upgrade) {
        PR_WARN("Ignoring empty OTA upgrade notification");
        return;
    }

    cJSON *type = cJSON_GetObjectItemCaseSensitive(upgrade, "type");
    PR_INFO("----- Upgrade information -----");
    PR_INFO("OTA Channel: %d", cJSON_IsNumber(type) ? type->valueint : -1);
    PR_INFO("Version: %s", upgrade_string(upgrade, "version"));
    PR_INFO("Size: %s", upgrade_string(upgrade, "size"));
    PR_INFO("MD5: %s", upgrade_string(upgrade, "md5"));
    PR_INFO("HMAC: %s", upgrade_string(upgrade, "hmac"));
    PR_INFO("URL: %s", upgrade_string(upgrade, "url"));
    PR_INFO("HTTPS URL: %s", upgrade_string(upgrade, "httpsUrl"));
}

/**
 * @brief user defined event handler
 *
 * @param client device info
 * @param event the event info
 * @return void
 */
void user_event_handler_on(tuya_iot_client_t *client, tuya_event_msg_t *event)
{
    if (NULL == client || NULL == event) {
        PR_ERR("Ignoring invalid Tuya event");
        return;
    }

    PR_DEBUG("Tuya Event ID:%d(%s)", event->id, EVENT_ID2STR(event->id));
    PR_INFO("Device Free heap %d", tal_system_get_free_heap_size());
    switch (event->id) {
    case TUYA_EVENT_BIND_START:
        PR_INFO("Device Bind Start!");
        status_led_set_paired(false);
        status_led_set_pairing(true);
        break;

    case TUYA_EVENT_ACTIVATE_SUCCESSED:
        PR_INFO("Device activation completed!");
        status_led_set_paired(true);
        status_led_set_pairing(false);
        status_led_update();
        break;

    /* Print the QRCode for Tuya APP bind */
    case TUYA_EVENT_DIRECT_MQTT_CONNECTED: {
#if defined(ENABLE_QRCODE) && (ENABLE_QRCODE == 1)
        char buffer[255];
        int written = snprintf(buffer, sizeof(buffer),
                               "https://smartapp.tuya.com/s/p?p=%s&uuid=%s&v=2.0",
                               TUYA_PRODUCT_ID,
                               NULL != license.uuid ? license.uuid : "");
        if (written < 0 || (size_t)written >= sizeof(buffer)) {
            PR_ERR("Pairing QR code URL is too long");
        } else {
            qrcode_string_output(buffer, user_log_output_cb, 0);
        }
#endif
    } break;

    /* MQTT with tuya cloud is connected, device online */
    case TUYA_EVENT_MQTT_CONNECTED:
        PR_INFO("Device MQTT Connected!");
        if (tuya_iot_activated(client)) {
            status_led_set_paired(true);
            status_led_set_pairing(false);
            status_led_update();
        }
        break;

    case TUYA_EVENT_BINDED_NOTIFY:
        PR_INFO("Device bound notification received!");
        status_led_set_paired(true);
        status_led_set_pairing(false);
        status_led_update();
        break;

    /* RECV upgrade request */
    case TUYA_EVENT_UPGRADE_NOTIFY:
        user_upgrade_notify_on(client, event->value.asJSON);
        break;

    /* Sync time with tuya Cloud */
    case TUYA_EVENT_TIMESTAMP_SYNC:
        PR_INFO("Sync timestamp:%d", event->value.asInteger);
        tal_time_set_posix(event->value.asInteger, 1);
        break;
    case TUYA_EVENT_RESET:
        PR_INFO("Device Reset:%d", event->value.asInteger);
        if (event->value.asInteger == TUYA_RESET_TYPE_REMOTE_UNACTIVE ||
            event->value.asInteger == TUYA_RESET_TYPE_LOCAL_UNACTIVE ||
            event->value.asInteger == TUYA_RESET_TYPE_FACTORY ||
            event->value.asInteger == TUYA_RESET_TYPE_REMOTE_FACTORY ||
            event->value.asInteger == TUYA_RESET_TYPE_DATA_FACTORY) {
            status_led_set_paired(false);
            status_led_set_pairing(true);
        }
        break;

    /* RECV OBJ DP */
    case TUYA_EVENT_DP_RECEIVE_OBJ: {
        dp_obj_recv_t *dpobj = event->value.dpobj;
        if (NULL == dpobj) {
            PR_ERR("Ignoring invalid object DP event");
            break;
        }
        PR_DEBUG("SOC Rev DP Cmd t1:%d t2:%d CNT:%u", dpobj->cmd_tp, dpobj->dtt_tp, dpobj->dpscnt);
        if (dpobj->devid != NULL) {
            PR_DEBUG("devid.%s", dpobj->devid);
        }

        uint32_t index = 0;
        for (index = 0; index < dpobj->dpscnt; index++) {
            dp_obj_t *dp = dpobj->dps + index;
            PR_DEBUG("idx:%d dpid:%d type:%d ts:%u", index, dp->id, dp->type, dp->time_stamp);
            dp_state_update(dp);
            switch (dp->type) {
            case PROP_BOOL: {
                PR_DEBUG("bool value:%d", dp->value.dp_bool);
                switch_ctrl_handle_dp(dp->id, dp->value.dp_bool);
                PR_NOTICE("Handle DP ID:%d value:%d", dp->id, dp->value.dp_bool);
                break;
            }
            case PROP_VALUE: {
                PR_DEBUG("int value:%d", dp->value.dp_value);
                break;
            }
            case PROP_STR: {
                PR_DEBUG("str value:%s", dp->value.dp_str);
                break;
            }
            case PROP_ENUM: {
                PR_DEBUG("enum value:%u", dp->value.dp_enum);
                break;
            }
            case PROP_BITMAP: {
                PR_DEBUG("bits value:0x%X", dp->value.dp_bitmap);
                break;
            }
            default: {
                PR_ERR("idx:%d dpid:%d type:%d ts:%u is invalid", index, dp->id, dp->type, dp->time_stamp);
                break;
            }
            } // end of switch
        }

        dp_report_obj(client, dpobj->devid, dpobj->dps, dpobj->dpscnt, 0);

    } break;

    /* RECV RAW DP */
    case TUYA_EVENT_DP_RECEIVE_RAW: {
        dp_raw_recv_t *dpraw = event->value.dpraw;
        if (NULL == dpraw) {
            PR_ERR("Ignoring empty raw DP event");
            break;
        }
        PR_DEBUG("SOC Rev DP Cmd t1:%d t2:%d", dpraw->cmd_tp, dpraw->dtt_tp);
        if (dpraw->devid != NULL) {
            PR_DEBUG("devid.%s", dpraw->devid);
        }

        uint32_t index = 0;
        dp_raw_t *dp = &dpraw->dp;
        PR_DEBUG("dpid:%d type:RAW len:%d data:", dp->id, dp->len);
        dp_state_store_raw(dp);
        for (index = 0; index < dp->len; index++) {
            PR_DEBUG_RAW("%02x", dp->data[index]);
        }

        dp_report_raw(client, dpraw->devid, &dpraw->dp, 3);

    } break;

        /* TBD.. add other event if necessary */

    default:
        break;
    }
}

bool user_network_check(void)
{
    netmgr_status_e status = NETMGR_LINK_DOWN;
    netmgr_conn_get(NETCONN_AUTO, NETCONN_CMD_STATUS, &status);
    return status == NETMGR_LINK_DOWN ? false : true;
}

void user_main(void)
{
    int rt = OPRT_OK;
    cJSON_InitHooks(&(cJSON_Hooks){.malloc_fn = tal_malloc, .free_fn = tal_free});
    tal_log_init(TAL_LOG_LEVEL_DEBUG, 1024, (TAL_LOG_OUTPUT_CB)tkl_log_output);

    PR_NOTICE("Application information:");
    PR_NOTICE("Project name:        %s", PROJECT_NAME);
    PR_NOTICE("App version:         %s", PROJECT_VERSION);
    PR_NOTICE("Compile time:        %s", __DATE__);
    PR_NOTICE("TuyaOpen version:    %s", OPEN_VERSION);
    PR_NOTICE("TuyaOpen commit-id:  %s", OPEN_COMMIT);
    PR_NOTICE("Platform chip:       %s", PLATFORM_CHIP);
    PR_NOTICE("Platform board:      %s", PLATFORM_BOARD);
    PR_NOTICE("Platform commit-id:  %s", PLATFORM_COMMIT);
    tal_kv_init(&(tal_kv_cfg_t){
        .seed = "vmlkasdh93dlvlcy",
        .key = "dflfuap134ddlduq",
    });
    tal_sw_timer_init();
    tal_workq_init();
    reset_netconfig_start();
    if (OPRT_OK != tuya_authorize_read(&license)) {
        license.uuid = TUYA_OPENSDK_UUID;
        license.authkey = TUYA_OPENSDK_AUTHKEY;
        PR_WARN("Replace the TUYA_OPENSDK_UUID and TUYA_OPENSDK_AUTHKEY contents, otherwise the demo cannot work.\n \
                Visit https://platform.tuya.com/purchase/index?type=6 to get the open-sdk uuid and authkey.");
    }

    rt = tuya_iot_init(&client, &(const tuya_iot_config_t){
                                    .software_ver = PROJECT_VERSION,
                                    .productkey = TUYA_PRODUCT_ID,
                                    .uuid = license.uuid,
                                    .authkey = license.authkey,
                                    .event_handler = user_event_handler_on,
                                    .network_check = user_network_check,
                                });
    if (OPRT_OK != rt) {
        PR_ERR("Tuya IoT initialization failed, ret=%d", rt);
        return;
    }
    rt = dp_report_init();
    if (OPRT_OK != rt) {
        PR_ERR("Failed to initialize DP report mutex, ret=%d", rt);
        return;
    }
    rt = dp_state_init();
    if (OPRT_OK != rt) {
        PR_ERR("Failed to initialize DP state mutex, ret=%d", rt);
        return;
    }
    switch_ctrl_set_client(&client);
    status_led_init();
    if (tuya_iot_activated(&client)) {
        status_led_set_paired(true);
        status_led_set_pairing(false);
    } else {
        status_led_set_paired(false);
        status_led_set_pairing(true);
    }

    dp_state_load();

    // network init
    netmgr_type_e type = 0;
#if defined(ENABLE_WIFI) && (ENABLE_WIFI == 1)
    type |= NETCONN_WIFI;
#endif
#if defined(ENABLE_WIRED) && (ENABLE_WIRED == 1)
    type |= NETCONN_WIRED;
#endif
#if defined(ENABLE_CELLULAR) && (ENABLE_CELLULAR == 1)
    type |= NETCONN_CELLULAR;
#endif
    netmgr_init(type);

#if defined(ENABLE_WIFI) && (ENABLE_WIFI == 1)
    netmgr_conn_set(NETCONN_WIFI, NETCONN_CMD_NETCFG, &(netcfg_args_t){.type = NETCFG_TUYA_BLE | NETCFG_TUYA_WIFI_AP});
#endif

    PR_DEBUG("tuya_iot_init success");
    /* Start tuya iot task */
    tuya_iot_start(&client);
    humi_sensor_start(&client);
    dp_restore_start();
    wifi_signal_start();
    reset_netconfig_check();

    while(1){
        tuya_iot_yield(&client);
    }
}

static THREAD_HANDLE ty_app_thread = NULL;

static void tuya_app_thread(void *arg)
{
    user_main();

    tal_thread_delete(ty_app_thread);
    ty_app_thread = NULL;
}

void tuya_app_main(void)
{
    // peripheral init
    switch_ctrl_init();

    THREAD_CFG_T thrd_param = {
        .stackDepth = 1024 * 16,
        .priority = 4,
        .thrdname = "tuya_app_main",
        .psram_mode = 0,
    };
    OPERATE_RET rt = tal_thread_create_and_start(
        &ty_app_thread,
        NULL,
        NULL,
        tuya_app_thread,
        NULL,
        &thrd_param
    );
    if (OPRT_OK != rt) {
        ty_app_thread = NULL;
        PR_ERR("Failed to create Tuya app task, ret=%d", rt);
    }
}
