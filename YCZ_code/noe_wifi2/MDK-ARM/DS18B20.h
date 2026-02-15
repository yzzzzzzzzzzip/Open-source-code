#ifndef __DS18B20_H__
#define __DS18B20_H__

#include "main.h"
#include "usart.h"
#include "gpio.h"
#include "esp8266.h"
/************************* 匹配当前引脚的宏定义 *************************/
#define DS18B20_DQ_GPIO_PORT    GPIOA
#define DS18B20_DQ_GPIO_PIN     GPIO_PIN_1

extern uint16_t tempMax;
extern uint16_t tempMin;
extern int16_t temperature;

/************************* 引脚电平操作宏定义 *************************/
#define DS18B20_DQ_HIGH()       HAL_GPIO_WritePin(DS18B20_DQ_GPIO_PORT, DS18B20_DQ_GPIO_PIN, GPIO_PIN_SET)
#define DS18B20_DQ_LOW()        HAL_GPIO_WritePin(DS18B20_DQ_GPIO_PORT, DS18B20_DQ_GPIO_PIN, GPIO_PIN_RESET)
#define DS18B20_DQ_READ()       HAL_GPIO_ReadPin(DS18B20_DQ_GPIO_PORT, DS18B20_DQ_GPIO_PIN)

/************************* 函数声明（包含新增的模式切换函数） *************************/
bool DS18B20_Rst(void);
uint8_t DS18B20_Check(void);
uint8_t DS18B20_Read_Bit(void);
uint8_t DS18B20_Read_Byte(void);
void DS18B20_Write_Byte(uint8_t dat);
void DS18B20_Start(void);
uint8_t DS18B20_Init(void);
int16_t DS18B20_Get_Temp(void);
void DS18B20_Delay_us(uint32_t us);
	
// GPIO方向切换宏
#define DS18B20_IO_OUT()  {GPIO_InitTypeDef GPIO_InitStruct; \
                           GPIO_InitStruct.Pin = DS18B20_DQ_GPIO_PIN; \
                           GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP; \
                           GPIO_InitStruct.Pull = GPIO_NOPULL; \
                           GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH; \
                           HAL_GPIO_Init(DS18B20_DQ_GPIO_PORT, &GPIO_InitStruct);}

#define DS18B20_IO_IN()   {GPIO_InitTypeDef GPIO_InitStruct; \
                           GPIO_InitStruct.Pin = DS18B20_DQ_GPIO_PIN; \
                           GPIO_InitStruct.Mode = GPIO_MODE_INPUT; \
                           GPIO_InitStruct.Pull = GPIO_NOPULL; \
                           HAL_GPIO_Init(DS18B20_DQ_GPIO_PORT, &GPIO_InitStruct);}

// 引脚电平操作宏
#define DS18B20_DQ_OUT(x)  HAL_GPIO_WritePin(DS18B20_DQ_GPIO_PORT, DS18B20_DQ_GPIO_PIN, x ? GPIO_PIN_SET : GPIO_PIN_RESET)
#define DS18B20_DQ_IN()    HAL_GPIO_ReadPin(DS18B20_DQ_GPIO_PORT, DS18B20_DQ_GPIO_PIN)
													 
#endif
