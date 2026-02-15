#include "dht11.h"
DHT11_Data_TypeDef DHT11_Data;
// 无需UART2句柄、无需UART2初始化，直接调用现成的UART2_Debug_Print

/**
 * @brief  DHT11引脚设为推挽输出模式
 * @retval 无
 */
void DHT11_GPIO_Out_Mode(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    
    DHT11_CLK_ENABLE();
    
    GPIO_InitStruct.Pin = DHT11_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(DHT11_PORT, &GPIO_InitStruct);
}

/**
 * @brief  DHT11引脚设为浮空输入模式
 * @retval 无
 */
void DHT11_GPIO_In_Mode(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    
    GPIO_InitStruct.Pin = DHT11_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;  // 临时启用内部上拉，辅助外部上拉
    HAL_GPIO_Init(DHT11_PORT, &GPIO_InitStruct);
}

/**
 * @brief  DHT11发送复位脉冲
 * @retval 0: Reset success; 1: Reset failed
 */
uint8_t DHT11_Reset(void)
{
    uint8_t retry = 0;
    
    // 1. 拉低总线20ms（符合DHT11要求）
    DHT11_GPIO_Out_Mode();
    HAL_GPIO_WritePin(DHT11_PORT, DHT11_PIN, GPIO_PIN_RESET);
    HAL_Delay(20);
    
    // 2. 释放总线，拉高30us
    HAL_GPIO_WritePin(DHT11_PORT, DHT11_PIN, GPIO_PIN_SET);
    DS18B20_Delay_us(30);
    
    // 3. 切换输入模式，检测DHT11应答（增加重试）
    DHT11_GPIO_In_Mode();
    while (HAL_GPIO_ReadPin(DHT11_PORT, DHT11_PIN) == GPIO_PIN_SET && retry < 200)
    {
        retry++;
        DS18B20_Delay_us(1);  // 等待DHT11拉低总线（应答窗口最长200us）
    }
    
    if(retry >= 200)
    {
        UART2_Debug_Print("DHT11 Reset Failed, No Response Pulse Received\r\n");
        return 1;
    }
    else
    {
        // 等待应答脉冲结束（拉低后拉高）
        while(HAL_GPIO_ReadPin(DHT11_PORT, DHT11_PIN) == GPIO_PIN_RESET);
        while(HAL_GPIO_ReadPin(DHT11_PORT, DHT11_PIN) == GPIO_PIN_SET);
        
        UART2_Debug_Print("DHT11 Reset Successful, Response Pulse Received\r\n");
        return 0;
    }
}

/**
 * @brief  Read single bit from DHT11
 * @retval 0/1
 */
uint8_t DHT11_Read_Bit(void)
{
    uint8_t bit_val = 0;
    
    while(HAL_GPIO_ReadPin(DHT11_PORT, DHT11_PIN) == GPIO_PIN_RESET);
    DS18B20_Delay_us(20);
    
    if(HAL_GPIO_ReadPin(DHT11_PORT, DHT11_PIN) == GPIO_PIN_SET)
    {
        bit_val = 1;
        while(HAL_GPIO_ReadPin(DHT11_PORT, DHT11_PIN) == GPIO_PIN_SET);
    }
    
    return bit_val;
}

/**
 * @brief  Read single byte from DHT11
 * @retval Read byte value
 */
uint8_t DHT11_Read_Byte(void)
{
    uint8_t i, byte_val = 0;
    
    for(i = 0; i < 8; i++)
    {
        byte_val <<= 1;
        byte_val |= DHT11_Read_Bit();
    }
    
    return byte_val;
}

/**
 * @brief  Read complete DHT11 data (5 bytes)
 * @param  dht11_data: Pointer to DHT11 data struct
 * @retval 0: Success and checksum passed; 1: Failed or checksum error
 */
uint8_t DHT11_Read_Data(DHT11_Data_TypeDef *dht11_data)
{
    uint8_t ret = 1;
    
    // 直接调用现成的UART2_Debug_Print，输出英文采集步骤
    UART2_Debug_Print("===== Start DHT11 Data Acquisition Process =====\r\n");
    UART2_Debug_Print("Step 1: Send DHT11 Reset Pulse\r\n");
    
    if(DHT11_Reset() != 0)
    {
        UART2_Debug_Print("Out Step 1 Failed: DHT11 Reset Failed\r\n");
        return ret;
    }
    
    UART2_Debug_Print("Step 2: Start Reading 5 Bytes of Data\r\n");
    dht11_data->humi_int  = DHT11_Read_Byte();
    dht11_data->humi_dec  = DHT11_Read_Byte();
    dht11_data->temp_int  = DHT11_Read_Byte();
    dht11_data->temp_dec  = DHT11_Read_Byte();
    dht11_data->check_sum = DHT11_Read_Byte();
    
    UART2_Debug_Print("Step 2 Completed: Humidity Int=0x%02X, Humidity Dec=0x%02X, Temp Int=0x%02X, Temp Dec=0x%02X, Checksum=0x%02X\r\n",
                dht11_data->humi_int, dht11_data->humi_dec,
                dht11_data->temp_int, dht11_data->temp_dec,
                dht11_data->check_sum);
    
    UART2_Debug_Print("Step 3: Verify Data Validity\r\n");
    uint8_t check_calc = dht11_data->humi_int + dht11_data->humi_dec +
                         dht11_data->temp_int + dht11_data->temp_dec;
    
    if(dht11_data->check_sum == check_calc)
    {
        ret = 0;
        UART2_Debug_Print("Step 3 Completed: Data Verification Passed\r\n");
    }
    else
    {
        UART2_Debug_Print("Step 3 Failed: Data Verification Error (Calculated=0x%02X, Read=0x%02X)\r\n",
                    check_calc, dht11_data->check_sum);
    }
    
    UART2_Debug_Print("===== DHT11 Data Acquisition Process End =====\r\n\r\n");
    return ret;
}
