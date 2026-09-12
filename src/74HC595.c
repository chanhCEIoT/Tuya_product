#include <stdio.h>
#include "74HC595.h"
#include "tal_system.h"
#include "tal_api.h"
#include "tkl_gpio.h"
#include "tuya_cloud_types.h"
int light_def = 0b00000000;
#define HC595_SCK_Low() tkl_gpio_write(SCK_GPIO_PIN, 0)  
#define HC595_SCK_High() tkl_gpio_write(SCK_GPIO_PIN, 1) 
#define HC595_RCK_Low() tkl_gpio_write(RCK_GPIO_PIN, 0)  
#define HC595_RCK_High() tkl_gpio_write(RCK_GPIO_PIN, 1)  
#define HC595_Data_Low() tkl_gpio_write(SDA_GPIO_PIN, 0)  
#define HC595_Data_High() tkl_gpio_write(SDA_GPIO_PIN, 1) 

static const char *TAG = "74HC595";

void HC595_Save(void)
{
    HC595_RCK_Low(); 
    tal_system_sleep(1); 
    HC595_RCK_High();
}


static const uint8_t NUM_MAP[10] = {
    0xC0, // 0
    0xF9, // 1
    0xA4, // 2
    0xB0, // 3
    0x99, // 4
    0x92, // 5
    0x82, // 6
    0xF8, // 7
    0x80, // 8
    0x90  // 9
};

static const uint8_t NUM_MAP_DOT[10] = {
    0x40, // 0. (0xC0 & 0x7F)
    0x79, // 1. (0xF9 & 0x7F)
    0x24, // 2. (0xA4 & 0x7F)
    0x30, // 3. (0xB0 & 0x7F)
    0x19, // 4. (0x99 & 0x7F)
    0x12, // 5. (0x92 & 0x7F)
    0x02, // 6. (0x82 & 0x7F)
    0x78, // 7. (0xF8 & 0x7F)
    0x00, // 8. (0x80 & 0x7F)
    0x10  // 9. (0x90 & 0x7F)
};

void HC595_Send_Byte(uint8_t byte)
{
    uint8_t i;
    for (i = 0; i < 8; i++) 
    {
 
        HC595_SCK_Low(); 
        if (byte & 0x80)
        {                     
            HC595_Data_High(); 
        }
        else
        { 
            HC595_Data_Low();
        }
        byte <<= 1;    
        HC595_SCK_High();
    }
}
void HC595_Send_Multi_Byte(uint8_t *data, uint16_t len)
{
    uint8_t i;

    for (i = 0; i < len; i++)
    {
        HC595_Send_Byte(data[i]);
    }
    HC595_Save();
}

void open_led(uint8_t byte)
{
    light_def |= (1 << byte);
    HC595_Send_Byte(light_def);
    HC595_Save();
}

void close_led(uint8_t byte)
{
    light_def &= ~(1 << byte);
    HC595_Send_Byte(light_def);
    HC595_Save();
}

void close_led_all()
{
    for (int i = 0; i < _74HC595_LEVEL * 8; i++)
    {
        close_led(i);
    }
}

void open_led_all()
{
    for (int i = 0; i < _74HC595_LEVEL * 8; i++)
    {
        open_led(i);
    }
}

void HC595_INIT(void)
{
    TUYA_GPIO_BASE_CFG_T gpio_cfg = {
        .mode = TUYA_GPIO_PUSH_PULL,
        .direct = TUYA_GPIO_OUTPUT,
        .level = TUYA_GPIO_LEVEL_LOW
    };
    
    // Tuya TKL khởi tạo GPIO từng chân độc lập
    tkl_gpio_init(SCK_GPIO_PIN, &gpio_cfg);
    tkl_gpio_init(RCK_GPIO_PIN, &gpio_cfg);
    tkl_gpio_init(SDA_GPIO_PIN, &gpio_cfg);
    
    HC595_SCK_Low();
    HC595_RCK_Low();
    HC595_Data_Low();
}
void HC595_Show_Number_Static(uint16_t number)
{

    uint8_t digits[4];
    digits[0] = (number / 1000) % 10; 
    digits[1] = (number / 100) % 10;  
    digits[2] = (number / 10) % 10;   
    digits[3] = number % 10;          
    HC595_Send_Byte(NUM_MAP[digits[3]]); 
    HC595_Send_Byte(NUM_MAP[digits[2]]); 
    HC595_Send_Byte(NUM_MAP_DOT[digits[1]]); 
    HC595_Send_Byte(NUM_MAP[digits[0]]); 
    HC595_Save();
}

void HC595_Show_Humidity(float humidity)
{
    uint16_t humidity_tenths;
    uint16_t humidity_whole;
    uint8_t display[4];

    if (humidity < 0.0f) {
        humidity = 0.0f;
    } else if (humidity > 100.0f) {
        humidity = 100.0f;
    }

    humidity_tenths = (uint16_t)(humidity * 10.0f + 0.5f);
    humidity_whole = humidity_tenths / 10;

    if (humidity_whole >= 100) {
        display[0] = NUM_MAP[1];
        display[1] = NUM_MAP[0];
        display[2] = NUM_MAP_DOT[0];
        display[3] = NUM_MAP[0];
    } else {
        display[0] = 0xFF;
        display[1] = humidity_whole >= 10 ? NUM_MAP[humidity_whole / 10] : 0xFF;
        display[2] = NUM_MAP_DOT[humidity_whole % 10];
        display[3] = NUM_MAP[humidity_tenths % 10];
    }

    HC595_Send_Byte(display[3]);
    HC595_Send_Byte(display[2]);
    HC595_Send_Byte(display[1]);
    HC595_Send_Byte(display[0]);
    HC595_Save();
}