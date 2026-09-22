#include "ads1115.h"

#include "tal_api.h"
#include "tkl_i2c.h"
#include "tkl_pinmux.h"

#define ADS1115_I2C_PORT       TUYA_I2C_NUM_0
#define ADS1115_SCL_PIN        TUYA_GPIO_NUM_22
#define ADS1115_SDA_PIN        TUYA_GPIO_NUM_21
#define ADS1115_ADDRESS        0x48
#define ADS1115_REG_CONVERSION 0x00
#define ADS1115_REG_CONFIG     0x01

#define ADS1115_CONFIG_OS_START       0x8000
#define ADS1115_CONFIG_MUX_AIN0_GND   0x4000
#define ADS1115_CONFIG_PGA_4V096      0x0200
#define ADS1115_CONFIG_MODE_SINGLE    0x0100
#define ADS1115_CONFIG_DATA_RATE_128  0x0080
#define ADS1115_CONFIG_COMP_DISABLE   0x0003

static OPERATE_RET ads1115_write_register(uint8_t reg, uint16_t value)
{
    uint8_t data[3] = {
        reg,
        (uint8_t)(value >> 8),
        (uint8_t)value,
    };

    return tkl_i2c_master_send(ADS1115_I2C_PORT, ADS1115_ADDRESS, data, sizeof(data), FALSE);
}

static OPERATE_RET ads1115_read_register(uint8_t reg, uint16_t *value)
{
    uint8_t data[2] = {0};
    OPERATE_RET rt = tkl_i2c_master_send(
        ADS1115_I2C_PORT,
        ADS1115_ADDRESS,
        &reg,
        sizeof(reg),
        TRUE
    );
    if (OPRT_OK != rt) {
        return rt;
    }

    rt = tkl_i2c_master_receive(
        ADS1115_I2C_PORT,
        ADS1115_ADDRESS,
        data,
        sizeof(data),
        FALSE
    );
    if (OPRT_OK == rt) {
        *value = ((uint16_t)data[0] << 8) | data[1];
    }
    return rt;
}

OPERATE_RET ads1115_init(void)
{
    TUYA_IIC_BASE_CFG_T cfg = {
        .role = TUYA_IIC_MODE_MASTER,
        .speed = TUYA_IIC_BUS_SPEED_100K,
        .addr_width = TUYA_IIC_ADDRESS_7BIT,
    };

    tkl_io_pinmux_config(ADS1115_SCL_PIN, TUYA_IIC0_SCL);
    tkl_io_pinmux_config(ADS1115_SDA_PIN, TUYA_IIC0_SDA);

    OPERATE_RET rt = tkl_i2c_init(ADS1115_I2C_PORT, &cfg);
    if (OPRT_OK != rt) {
        return rt;
    }

    uint16_t config = ADS1115_CONFIG_MUX_AIN0_GND |
                      ADS1115_CONFIG_PGA_4V096 |
                      ADS1115_CONFIG_MODE_SINGLE |
                      ADS1115_CONFIG_DATA_RATE_128 |
                      ADS1115_CONFIG_COMP_DISABLE;
    rt = ads1115_write_register(ADS1115_REG_CONFIG, config);
    if (OPRT_OK == rt) {
        PR_INFO("ADS1115 initialized: address=0x%02X, AIN0, PGA=4.096V", ADS1115_ADDRESS);
    }
    return rt;
}

OPERATE_RET ads1115_read_adc0(int32_t *value)
{
    if (NULL == value) {
        return OPRT_INVALID_PARM;
    }

    uint16_t config = ADS1115_CONFIG_OS_START |
                      ADS1115_CONFIG_MUX_AIN0_GND |
                      ADS1115_CONFIG_PGA_4V096 |
                      ADS1115_CONFIG_MODE_SINGLE |
                      ADS1115_CONFIG_DATA_RATE_128 |
                      ADS1115_CONFIG_COMP_DISABLE;
    OPERATE_RET rt = ads1115_write_register(ADS1115_REG_CONFIG, config);
    if (OPRT_OK != rt) {
        return rt;
    }

    tal_system_sleep(10);

    uint16_t raw = 0;
    rt = ads1115_read_register(ADS1115_REG_CONVERSION, &raw);
    if (OPRT_OK == rt) {
        int32_t signed_raw = (int16_t)raw;
        if (signed_raw < 0) {
            signed_raw = 0;
        }
        *value = (signed_raw * 4095L) / 32767L;
    }
    return rt;
}