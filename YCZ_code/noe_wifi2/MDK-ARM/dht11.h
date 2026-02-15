#ifndef __DHT11_H
#define __DHT11_H

#include "main.h"
#include "usart.h"
#include "gpio.h"
#include "esp8266.h"
#include "DS18B20.h"
// DHT11引脚定义（PA0）
#define DHT11_PIN        GPIO_PIN_0
#define DHT11_PORT       GPIOA
#define DHT11_CLK_ENABLE()  __HAL_RCC_GPIOA_CLK_ENABLE()

// DHT11数据存储结构
typedef struct
{
    uint8_t humi_int;   // 湿度整数部分
    uint8_t humi_dec;   // 湿度小数部分（DHT11此值恒为0）
    uint8_t temp_int;   // 温度整数部分
    uint8_t temp_dec;   // 温度小数部分（DHT11此值恒为0）
    uint8_t check_sum;  // 校验和
} DHT11_Data_TypeDef;
extern DHT11_Data_TypeDef DHT11_Data;

// 函数声明
void DHT11_GPIO_Out_Mode(void);  // DHT11引脚设为输出模式
void DHT11_GPIO_In_Mode(void);   // DHT11引脚设为输入模式
uint8_t DHT11_Reset(void);       // DHT11发送复位脉冲
uint8_t DHT11_Read_Bit(void);    // 读取DHT11单个位
uint8_t DHT11_Read_Byte(void);   // 读取DHT11单个字节
uint8_t DHT11_Read_Data(DHT11_Data_TypeDef *dht11_data);  // 读取完整温湿度数据


#endif
