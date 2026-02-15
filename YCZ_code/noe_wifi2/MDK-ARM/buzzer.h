#ifndef __BUZZER_H
#define __BUZZER_H

#include "main.h"
#include "usart.h"
#include "gpio.h"
#include "esp8266.h"
#include "DS18B20.h"
#include "tim.h"

#define BUZZER_TIM_CH   TIM_CHANNEL_1
#define pwm_pulse        2225
/************************ 函数声明 ************************/
/**
 * @brief  无源蜂鸣器PWM初始化（GPIO+TIM3配置）
 * @retval 无
 */
void Buzzer_TIM3_Init(void);

/**
 * @brief  无源蜂鸣器开启发声（输出2kHz 50%占空比PWM）
 * @retval 无
 */
void Buzzer_On(void);

/**
 * @brief  无源蜂鸣器停止发声（关闭PWM输出，占空比置0）
 * @retval 无
 */
void Buzzer_Off(void);


#endif
