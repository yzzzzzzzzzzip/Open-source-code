#include "DS18B20.h"
// 全局变量（保持原定义）
uint16_t tempMax = 373;  // 温度上限（0.1℃单位）
uint16_t tempMin = 150;  // 温度下限（0.1℃单位）
int16_t temperature = 0; // 当前温度（0.1℃单位）
/************************* 微秒延时函数（关键） *************************/
// * @brief  通用微秒级延时函数（HAL库兼容，不破坏SysTick）

void DS18B20_Delay_us(uint32_t us)
{
    // 1. 保存SysTick原始配置，避免影响HAL_Delay()
    uint32_t old_SysTick_CTRL = SysTick->CTRL;
    uint32_t old_SysTick_LOAD = SysTick->LOAD;
    uint32_t old_SysTick_VAL  = SysTick->VAL;
    
    // 2. 计算延时所需节拍数（SystemCoreClock为系统时钟，如72MHz）
    // SysTick默认分频8，即时钟频率为SystemCoreClock/8
    uint32_t ticks = us * (SystemCoreClock / 1000000U) / 8U;
    if(ticks == 0) ticks = 1;  // 避免us=0时出现无效延时
    
    // 3. 配置SysTick进行单次延时
    SysTick->LOAD = ticks - 1;  // 装载延时计数值
    SysTick->VAL  = 0;          // 清空当前计数值
    SysTick->CTRL = SysTick_CTRL_ENABLE_Msk;  // 仅使能SysTick，关闭中断（不影响HAL库）
    
    // 4. 等待延时完成（检测计数完成标志位）
    while(!(SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk));
    
    // 5. 恢复SysTick原始配置，保证HAL_Delay()正常工作
    SysTick->CTRL = old_SysTick_CTRL;
    SysTick->LOAD = old_SysTick_LOAD;
    SysTick->VAL  = old_SysTick_VAL;
}

/************************* DS18B20复位并检测存在应答 *************************/
bool DS18B20_Reset(void)
{
    uint8_t retry = 0;
    uint32_t ack_start = 0, ack_duration = 0;
    
    // 1. 主机拉低总线 ≥480μs（官方要求，这里取500μs，符合要求）
    DS18B20_IO_OUT();  // 先设为输出模式，确保电平可控
    DS18B20_DQ_LOW();
    DS18B20_Delay_us(500);  // 满足复位脉冲≥480μs的要求
    
    // 2. 主机释放总线，进入接收状态（等待从机应答）
    DS18B20_DQ_HIGH();
    DS18B20_Delay_us(20);   // 等待15~60μs（取中间值20μs，符合要求）
    DS18B20_IO_IN();   // 切换为输入模式，读取从机电平
    
    // 3. 检测从机拉低总线的应答脉冲（需持续60~240μs）
    while(DS18B20_DQ_READ() == GPIO_PIN_SET && retry < 250)  // 最大等待250μs，覆盖240μs上限
    {
        retry++;
        DS18B20_Delay_us(1);
    }
    
    if(retry >= 250)
    {
        return 1;  // 未检测到从机应答，初始化失败
    }
    else
    {
        // 4. 记录应答开始时间，检测应答时长是否在60~240μs范围内
        ack_start = HAL_GetTick();
        while(DS18B20_DQ_READ() == GPIO_PIN_RESET && retry < 250)
        {
            retry++;
            DS18B20_Delay_us(1);
        }
        ack_duration = retry;  // 得到从机拉低总线的时长
        
        // 5. 验证应答时长是否符合官方要求（60~240μs）
        if(ack_duration < 60 || ack_duration > 240)
        {
            return 1;  // 应答时长异常，初始化失败
        }
        
        UART2_Debug_Print("DS18B20_Reset Success (Ack:%dus)", ack_duration);
        return 0;  // 初始化成功，符合所有时序要求
    }
}
