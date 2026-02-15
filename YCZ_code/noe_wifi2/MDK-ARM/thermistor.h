#ifndef __THERMISTOR_H
#define __THERMISTOR_H

#include "main.h"
#include "usart.h"
#include "gpio.h"
#include "esp8266.h"
#include "DS18B20.h"
#include "tim.h"
/************************ 热敏电阻模块DO脚配置（可根据实际修改） ************************/
// 示例：DO脚接PA1，可自行修改端口和引脚
#define THERMISTOR_DO_PORT    GPIOA
#define THERMISTOR_DO_PIN     GPIO_PIN_1
#define THERMISTOR_DO_CLK_EN()  __HAL_RCC_GPIOA_CLK_ENABLE()

/************************ 函数声明 ************************/


void SR04_trig(void);

void SR04_Init(void);
	



/**
 * @brief  热敏电阻模块DO脚GPIO初始化（配置为输入模式）
 * @retval 无
 */
void Thermistor_DO_Init(void);

/**
 * @brief  读取热敏电阻模块DO脚电平状态
 * @retval GPIO_PIN_SET（高电平） / GPIO_PIN_RESET（低电平）
 */
GPIO_PinState Thermistor_DO_Read_Level(void);

/**
 * @brief  判断温度是否超过模块预设阈值
 * @retval 0：温度正常（未超限）；1：温度超限（超过预设阈值）
 */
uint8_t Thermistor_Check_OverTemp(void);

#endif
