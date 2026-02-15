#include "thermistor.h"
#include "gpio.h"



void SR04_trig(void){

		HAL_GPIO_WritePin(TRIG_GPIO_Port, TRIG_Pin, GPIO_PIN_SET);
		HAL_Delay(1);
		HAL_GPIO_WritePin(TRIG_GPIO_Port, TRIG_Pin, GPIO_PIN_RESET);
		__HAL_TIM_SET_COUNTER(&htim2, 0);	
	
}

void SR04_Init(void){
		HAL_TIM_Base_Start(&htim2);
		__HAL_TIM_SET_COUNTER(&htim2, 0);	
		HAL_TIM_IC_Start(&htim2, TIM_CHANNEL_3);		
		HAL_TIM_IC_Start_IT(&htim2, TIM_CHANNEL_4);		

}


/**
 * @brief  热敏电阻模块DO脚GPIO初始化（配置为浮空输入模式）
 * @retval 无
 */
void Thermistor_DO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    
    // 1. 使能GPIO时钟
    THERMISTOR_DO_CLK_EN();
    
    // 2. 配置DO脚为浮空输入模式（模块自身带上下拉，无需内部上拉/下拉）
    GPIO_InitStruct.Pin = THERMISTOR_DO_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;  // 浮空输入，适配模块自身电平
    HAL_GPIO_Init(THERMISTOR_DO_PORT, &GPIO_InitStruct);
}

/**
 * @brief  读取热敏电阻模块DO脚电平状态
 * @retval GPIO_PIN_SET（高电平） / GPIO_PIN_RESET（低电平）
 */
GPIO_PinState Thermistor_DO_Read_Level(void)
{
    // 直接调用HAL库函数读取GPIO输入电平
    return HAL_GPIO_ReadPin(THERMISTOR_DO_PORT, THERMISTOR_DO_PIN);
}

/**
 * @brief  判断温度是否超过模块预设阈值
 * @note   模块默认逻辑（可根据实际修改）：
 *         温度 < 预设阈值 → DO脚输出 高电平（GPIO_PIN_SET）→ 温度正常
 *         温度 ≥ 预设阈值 → DO脚输出 低电平（GPIO_PIN_RESET）→ 温度超限
 * @retval 0：温度正常（未超限）；1：温度超限（超过预设阈值）
 */
uint8_t Thermistor_Check_OverTemp(void)
{
    GPIO_PinState do_level = Thermistor_DO_Read_Level();
    
    if(do_level == GPIO_PIN_RESET)
    {
        // 温度超限，可调用现成的UART2_Debug_Print打印提示
        UART2_Debug_Print("Thermistor: Temperature Over Threshold (DO=LOW)\r\n");
        return 1;
    }
    else
    {
        // 温度正常，可选打印提示（如需精简，可注释）
        UART2_Debug_Print("Thermistor: Temperature Normal (DO=HIGH)\r\n");
        return 0;
    }
}
