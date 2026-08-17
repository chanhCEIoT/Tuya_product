#ifndef LINQQ001_74HC595_H
#define LINQQ001_74HC595_H

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "tuya_cloud_types.h" // Thêm thư viện kiểu dữ liệu chuẩn của Tuya

// Định nghĩa các chân GPIO (Thay đổi tùy theo thiết kế phần cứng của module Tuya)
#define SCK_GPIO_PIN 26  // Shift clock pin (SHCP / SCLK)
#define RCK_GPIO_PIN 14  // Latch/Storage clock pin (STCP / LOAD)
#define SDA_GPIO_PIN 33  // Data pin (DS / SDI)

// Số lượng IC 74HC595 mắc nối tiếp
#define _74HC595_LEVEL 4 

#ifdef __cplusplus
extern "C"
{
#endif

    // Các hàm khởi tạo và giao tiếp cơ bản
    void HC595_INIT(void);
    void HC595_Send_Byte(uint8_t data);
    void HC595_Send_Multi_Byte(uint8_t *data, uint16_t len);

    // Các hàm điều khiển LED đơn lẻ và toàn bộ
    void open_led(uint8_t bit_pos);
    void close_led(uint8_t bit_pos);
    void open_led_all(void);
    void close_led_all(void);

    // Hàm hiển thị LED 7 đoạn
    void HC595_Show_Number_Static(uint16_t number);

#ifdef __cplusplus
}
#endif

#endif // LINQQ001_74HC595_H