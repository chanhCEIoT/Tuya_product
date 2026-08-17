#include "humi_sensor.h"

#include <string.h>

#include "tal_api.h"
#include "tkl_adc.h"
#include "tuya_iot_dp.h"

#define ADC_PORT        TUYA_ADC_NUM_0
#define ADC_CHANNEL     4
#define ADC_MODE        TUYA_ADC_SINGLE
#define ADC_TYPE        TUYA_ADC_EXTERNAL_SAMPLE_VOL
#define ADC_WIDTH       12
#define ADC_CONV_CNT    1
#define ADC_DPID        121
#define ADC_AVG_BUF_LEN 20
#define SCALE_FACTOR 10
static float adc_avg = 0.0f;
static int adc_buf[ADC_AVG_BUF_LEN] = {0};
static int adc_count = 0;
static int adc_idx = 0;
static tuya_iot_client_t *s_adc_client = NULL;
static THREAD_HANDLE s_adc_thread = NULL;

static TUYA_ADC_BASE_CFG_T s_adc_cfg = {
    .ch_list.data = (1U << ADC_CHANNEL),
    .ch_nums      = 1,
    .width        = ADC_WIDTH,
    .mode         = ADC_MODE,
    .type         = ADC_TYPE,
    .conv_cnt     = ADC_CONV_CNT,
};

static OPERATE_RET adc_report_value(int32_t adc_val)
{
    if (NULL == s_adc_client) {
        PR_ERR("ADC report failed: client is NULL");
        return OPRT_INVALID_PARM;
    }

    if (s_adc_client->activate.devid[0] == '\0') {
        PR_ERR("ADC report failed: device ID is empty");
        return OPRT_INVALID_PARM;
    }

    dp_obj_t dp = {0};

    dp.id = ADC_DPID;
    dp.type = PROP_VALUE;
    dp.value.dp_value = (int)adc_val;

    return tuya_iot_dp_obj_report(
        s_adc_client,
        s_adc_client->activate.devid,
        &dp,
        1,
        0
    );
}

static int adc_get_average(int adc_val)
{
    int sum = 0;

    adc_buf[adc_idx] = adc_val;

    adc_idx++;
    if (adc_idx >= ADC_AVG_BUF_LEN) {
        adc_idx = 0;
    }

    if (adc_count < ADC_AVG_BUF_LEN) {
        adc_count++;
    }

    for (int i = 0; i < adc_count; i++) {
        sum += adc_buf[i];
    }

    return sum / adc_count;
}

static void adc_sensor_task(void *arg)
{
    OPERATE_RET rt;
    int32_t adc_val = 0;

    (void)arg;

    PR_INFO("ADC task started");

    PR_INFO(
        "Initializing ADC: port=%d, channel=%d",
        ADC_PORT,
        ADC_CHANNEL
    );

    rt = tkl_adc_init(ADC_PORT, &s_adc_cfg);
    if (OPRT_OK != rt) {
        PR_ERR(
            "tkl_adc_init failed, ret=%d",
            rt
        );
        s_adc_thread = NULL;
        return;
    }

    PR_INFO("ADC initialized successfully");
    while (1) {
        adc_val = 0;
        rt = tkl_adc_read_single_channel(ADC_PORT, ADC_CHANNEL,&adc_val);
        if (OPRT_OK == rt) {
            if (adc_val < 0) adc_val = 0;
            if (adc_val > 3355) adc_val = 3355;
            adc_avg = adc_get_average(adc_val);
            float humi_value = (adc_avg/3355)*100;
            PR_INFO("ADC channel %d value = %ld , humidity = %.2f", ADC_CHANNEL,(long) adc_avg, humi_value);
            if (NULL != s_adc_client) {
                rt = adc_report_value(humi_value * SCALE_FACTOR);
                if (OPRT_OK != rt) { PR_ERR("ADC DP report failed, ret=%d",rt);}
            }
        } 
        else {PR_ERR("tkl_adc_read_single_channel failed, ret=%d",rt);}
        tal_system_sleep(500);
    }
    tkl_adc_deinit(ADC_PORT);
    s_adc_thread = NULL;
}

void humi_sensor_start(tuya_iot_client_t *client )
{
    if (NULL == client) {
        PR_ERR(
            "ADC sensor start failed: client is NULL"
        );
        return;
    }
    s_adc_client = client;

    if (NULL != s_adc_thread) {
        PR_WARN(
            "ADC sensor task already started"
        );
        return;
    }

    /*
     * Thread configuration
     */
    THREAD_CFG_T thrd_param = {0};

    thrd_param.stackDepth = 1024 * 10;
    thrd_param.priority   = THREAD_PRIO_1;
    thrd_param.thrdname   = "adc_sensor_task";

    OPERATE_RET rt;

    rt = tal_thread_create_and_start(
        &s_adc_thread,
        NULL,
        NULL,
        adc_sensor_task,
        NULL,
        &thrd_param
    );

    if (OPRT_OK != rt) {
        PR_ERR(
            "Failed to create ADC task, ret=%d",
            rt
        );

        s_adc_thread = NULL;
        return;
    }

    PR_INFO("ADC sensor task created successfully");
}