#include "modbus_temp_humi_sensor.h"

#include <string.h>

#include "tal_api.h"
#include "tkl_pinmux.h"
#include "tuya_iot_dp.h"

#define MODBUS_485_UART_NUM        TUYA_UART_NUM_1
#define MODBUS_485_UART_RX_PIN     TUYA_IO_PIN_17
#define MODBUS_485_UART_TX_PIN     TUYA_IO_PIN_18
#define MODBUS_485_BAUDRATE        4800
#define MODBUS_485_SLAVE_ID_0        1
#define MODBUS_485_SLAVE_ID_1        2
#define MODBUS_485_START_REGISTER  0x0000
#define MODBUS_485_TEMP_DPID_0       123
#define MODBUS_485_HUMI_DPID_0       124
#define MODBUS_485_ID_DPID_0         120
#define MODBUS_485_TEMP_DPID_1       126
#define MODBUS_485_HUMI_DPID_1       127
#define MODBUS_485_ID_DPID_1         121
#define FACTOR_10 10

static tuya_iot_client_t *s_modbus_client = NULL;
static THREAD_HANDLE s_modbus_thread = NULL;

static uint16_t modbus_crc16(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFF;

    for (uint16_t pos = 0; pos < len; pos++) {
        crc ^= data[pos];
        for (uint8_t i = 0; i < 8; i++) {
            if (crc & 0x0001) {
                crc >>= 1;
                crc ^= 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }

    return crc;
}

static OPERATE_RET modbus_read_temp_humi(uint8_t slave_id, uint16_t reg_addr, uint16_t *temp, uint16_t *humi)
{
    uint8_t request[8] = {0};
    uint8_t response[9] = {0};
    int write_len = 0;
    int read_len = 0;

    request[0] = slave_id;
    request[1] = 0x03;
    request[2] = (uint8_t)(reg_addr >> 8);
    request[3] = (uint8_t)(reg_addr & 0xFF);
    request[4] = 0x00;
    request[5] = 0x02;

    uint16_t crc = modbus_crc16(request, 6);
    request[6] = (uint8_t)(crc & 0xFF);
    request[7] = (uint8_t)(crc >> 8);

    write_len = tal_uart_write(MODBUS_485_UART_NUM, (const uint8_t *)request, sizeof(request));
    if (write_len != (int)sizeof(request)) {
        PR_ERR("modbus uart write failed, len:%d", write_len);
        return OPRT_COM_ERROR;
    }

    read_len = tal_uart_read(MODBUS_485_UART_NUM, response, sizeof(response));
    if (read_len != (int)sizeof(response)) {
        PR_ERR("modbus uart read failed or incomplete, len:%d", read_len);
        return OPRT_COM_ERROR;
    }

    if (modbus_crc16(response, 7) != (uint16_t)((uint16_t)response[7] | ((uint16_t)response[8] << 8))) {
        PR_ERR("modbus crc check failed");
        return OPRT_COM_ERROR;
    }

    *humi = ((uint16_t)response[3] << 8) | response[4];
    *temp = ((uint16_t)response[5] << 8) | response[6];

    return OPRT_OK;
}

static OPERATE_RET modbus_report_temp_humi_0(uint16_t temp, uint16_t humi)
{
    if (NULL == s_modbus_client || s_modbus_client->activate.devid[0] == '\0') {
        return OPRT_INVALID_PARM;
    }

    dp_obj_t dps[3] = {0};

    dps[0].id = MODBUS_485_TEMP_DPID_0;
    dps[0].type = PROP_VALUE;
    dps[0].value.dp_value = temp*FACTOR_10;

    dps[1].id = MODBUS_485_HUMI_DPID_0;
    dps[1].type = PROP_VALUE;
    dps[1].value.dp_value = humi*FACTOR_10;

    dps[2].id = MODBUS_485_ID_DPID_0;
    dps[2].type = PROP_VALUE;
    dps[2].value.dp_value = MODBUS_485_SLAVE_ID_0;

    return tuya_iot_dp_obj_report(s_modbus_client, s_modbus_client->activate.devid, dps, 3, 0);
}
static OPERATE_RET modbus_report_temp_humi_1(uint16_t temp, uint16_t humi)
{
    if (NULL == s_modbus_client || s_modbus_client->activate.devid[0] == '\0') {
        return OPRT_INVALID_PARM;
    }

    dp_obj_t dps[3] = {0};

    dps[0].id = MODBUS_485_TEMP_DPID_1;
    dps[0].type = PROP_VALUE;
    dps[0].value.dp_value = temp*FACTOR_10;

    dps[1].id = MODBUS_485_HUMI_DPID_1;
    dps[1].type = PROP_VALUE;
    dps[1].value.dp_value = humi*FACTOR_10;

    dps[2].id = MODBUS_485_ID_DPID_1;
    dps[2].type = PROP_VALUE;
    dps[2].value.dp_value = MODBUS_485_SLAVE_ID_1;

    return tuya_iot_dp_obj_report(s_modbus_client, s_modbus_client->activate.devid, dps, 3, 0);
}

static void modbus_temp_humi_task(void *arg)
{
    OPERATE_RET rt = OPRT_OK;
    uint16_t temp = 0;
    uint16_t humi = 0;

    tkl_io_pinmux_config(MODBUS_485_UART_RX_PIN, TUYA_UART1_RX);
    tkl_io_pinmux_config(MODBUS_485_UART_TX_PIN, TUYA_UART1_TX);

    TAL_UART_CFG_T cfg = {0};
    cfg.base_cfg.baudrate = MODBUS_485_BAUDRATE;
    cfg.base_cfg.databits = TUYA_UART_DATA_LEN_8BIT;
    cfg.base_cfg.stopbits = TUYA_UART_STOP_LEN_1BIT;
    cfg.base_cfg.parity = TUYA_UART_PARITY_TYPE_NONE;
    cfg.rx_buffer_size = 256;
    cfg.open_mode = O_BLOCK;

    rt = tal_uart_init(MODBUS_485_UART_NUM, &cfg);
    if (OPRT_OK != rt) {
        PR_ERR("tal_uart_init failed, ret:%d", rt);
        return;
    }

    while (1) {
        rt = modbus_read_temp_humi(MODBUS_485_SLAVE_ID_0, MODBUS_485_START_REGISTER, &temp, &humi);
        if (OPRT_OK == rt) {
            PR_INFO("sensor raw temp:%u, humi:%u", temp, humi);
            PR_INFO("sensor temp:%u.%u C, humi:%u.%u %%", temp / 10, temp % 10, humi / 10, humi % 10);

            rt = modbus_report_temp_humi_0(temp, humi);
            if (OPRT_OK != rt) {
                PR_ERR("modbus dp report failed, ret:%d", rt);
            }
        } else {
            PR_ERR("modbus read failed, ret:%d", rt);
        }
        rt = modbus_read_temp_humi(MODBUS_485_SLAVE_ID_1, MODBUS_485_START_REGISTER, &temp, &humi);
        if (OPRT_OK == rt) {
            PR_INFO("sensor raw temp:%u, humi:%u", temp, humi);
            PR_INFO("sensor temp:%u.%u C, humi:%u.%u %%", temp / 10, temp % 10, humi / 10, humi % 10);

            rt = modbus_report_temp_humi_1(temp, humi);
            if (OPRT_OK != rt) {
                PR_ERR("modbus dp report failed, ret:%d", rt);
            }
        } else {
            PR_ERR("modbus read failed, ret:%d", rt);
        }
        tal_system_sleep(1000);
    }
}

void modbus_temp_humi_sensor_start(tuya_iot_client_t *client)
{
    if (NULL == client) {
        PR_ERR("modbus sensor start failed: client is null");
        return;
    }

    s_modbus_client = client;

    if (NULL != s_modbus_thread) {
        PR_WARN("modbus temp/humi sensor already started");
        return;
    }

    THREAD_CFG_T thrd_param = {
        .stackDepth = 1024 * 16,
        .priority = 5,
        .thrdname = "modbus_temp_humi",
        .psram_mode = 0,
    };
    tal_thread_create_and_start(&s_modbus_thread, NULL, NULL, modbus_temp_humi_task, NULL, &thrd_param);
}