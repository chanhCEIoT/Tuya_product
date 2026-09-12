#include "humi_sensor.h"

#include <string.h>
#include <stdio.h>
#include "tal_api.h"
#include "tkl_adc.h"
#include "tuya_iot_dp.h"
#include "74HC595.h"
#include "switch_ctrl.h"
#include "dp_state.h"
#include "dp_report.h"
#include <math.h>
#define ADC_PORT        TUYA_ADC_NUM_0
#define ADC_GPIO        32
#define ADC_CHANNEL     4
#define ADC_MODE        TUYA_ADC_SINGLE
#define ADC_TYPE        TUYA_ADC_EXTERNAL_SAMPLE_VOL
#define ADC_WIDTH       12
#define ADC_CONV_CNT    1
#define ADC_DPID        121
#define KALMAN_PROCESS_NOISE 1.0f
#define KALMAN_MEASUREMENT_NOISE 1000.0f
#define HUMIDITY_STEP 0.05f
#define SCALE_FACTOR 10
static float adc_avg = 0.0f;
static float kalman_estimate = 0.0f;
static float kalman_error = 0.0f;
static bool kalman_initialized = false;
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

static OPERATE_RET read_adc(int32_t *adc_value)
{
    if (NULL == adc_value) {
        return OPRT_INVALID_PARM;
    }
    return tkl_adc_read_single_channel(ADC_PORT, ADC_CHANNEL, adc_value);
}

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

    return dp_report_obj(
        s_adc_client,
        s_adc_client->activate.devid,
        &dp,
        1,
        0
    );
}

static float adc_kalman_filter(float measurement)
{
    if (!kalman_initialized) {
        kalman_estimate = measurement;
        kalman_error = KALMAN_MEASUREMENT_NOISE;
        kalman_initialized = true;
        return kalman_estimate;
    }

    kalman_error += KALMAN_PROCESS_NOISE;

    float gain = kalman_error / (kalman_error + KALMAN_MEASUREMENT_NOISE);
    kalman_estimate += gain * (measurement - kalman_estimate);
    kalman_error *= 1.0f - gain;

    return kalman_estimate;
}

static void adc_sensor_task(void *arg)
{
    OPERATE_RET rt;
    int32_t adc_val = 0;
    (void)arg;

    PR_INFO("ADC task started");

    PR_INFO(
        "Initializing ADC: GPIO=%d, port=%d, channel=%d",
        ADC_GPIO,
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
    int counter = 0;

    while (1) {
        adc_val = 0;

        rt = read_adc(&adc_val);

        if (OPRT_OK == rt) {
            adc_avg = adc_kalman_filter((float)adc_val);
            float humi_value = adc_avg / 4095.0f * 100.0f;
            humi_value += tuya_humidity_offset_get();
            if (humi_value < 0.0f) {
                humi_value = 0.0f;
            }
            humi_value = roundf(humi_value / HUMIDITY_STEP) * HUMIDITY_STEP;
            if (humi_value >= 100.0f) {
                humi_value = 99.99f;
            }

            switch_ctrl_auto_humidity(humi_value);
            
            HC595_Show_Number_Static((uint16_t)(humi_value * 100));
            PR_INFO("ADC channel %d raw = %ld, filtered = %.2f, humidity = %.2f",
                    ADC_CHANNEL, (long)adc_val, adc_avg, humi_value);
            if (NULL != s_adc_client) {
                counter++;
                if(counter > 20){
                rt = adc_report_value(humi_value * SCALE_FACTOR);
                if (OPRT_OK != rt) { PR_ERR("ADC DP report failed, ret=%d",rt);}
                counter = 0;
                }
            }
        } 
        else {PR_ERR("tkl_adc_read_single_channel failed, ret=%d",rt);}
        tal_system_sleep(200);
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
    HC595_INIT();

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