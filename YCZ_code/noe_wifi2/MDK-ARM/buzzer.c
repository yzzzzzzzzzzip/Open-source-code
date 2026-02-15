#include "buzzer.h"
/**
 * @brief  计算定时器PSC和ARR值，初始化TIM3生成指定频率PWM
 * @note   系统时钟默认72MHz（STM32F103），TIM3挂载于APB1（36MHz）
 * @retval 无
 */
void Buzzer_TIM3_Init(void)
{
    // 3. 开启PWM通道输出（初始占空比0，无声音）
    HAL_TIM_PWM_Start(&htim3, BUZZER_TIM_CH);
}

/**
 * @brief  无源蜂鸣器开启发声（设置50%占空比，输出2kHz方波）
 * @retval 无
 */
void Buzzer_On(void)
{
    
    // 设置PWM占空比（50%），开启发声
    __HAL_TIM_SET_COMPARE(&htim3, BUZZER_TIM_CH, pwm_pulse);
    
}

/**
 * @brief  无源蜂鸣器停止发声（设置0%占空比，关闭PWM输出）
 * @retval 无
 */
void Buzzer_Off(void)
{
    // 设置PWM占空比为0，停止发声
    __HAL_TIM_SET_COMPARE(&htim3, BUZZER_TIM_CH, 0);
    
}



