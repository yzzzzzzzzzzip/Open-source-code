/************************ 头文件包含 ************************/
#include "mq_gas_sensor.h"
#include <math.h>  // 用于对数（log10f）和指数（powf）运算

/************************ 静态辅助函数（内部私有，不对外暴露） ************************/
/**
 * @brief  将ADC数字量转换为传感器输出电压Vout
 * @param  adc_value: ADC采集的数字量（uint16_t）
 * @retval 传感器输出电压（单位：V，float）
 */
static float ADC_To_Voltage(uint16_t adc_value)
{
    // 电压转换公式：Vout = (ADC值 / ADC最大分辨率) * 参考电压
    return ((float)adc_value / ADC_RESOLUTION) * VREF;
}

/************************ 核心函数实现（对应头文件声明） ************************/
GasConcentration_t Calculate_Gas_Concentration(uint16_t Joystick_ADC[2])
{
    GasConcentration_t gas_conc = {0.0f, 0.0f};
    float vout_mq7, vout_mq8;
    float rs_mq7, rs_mq8;
    float rs_r0_ratio_mq7, rs_r0_ratio_mq8;

    // 步骤1：转换MQ-7的ADC值为Vout，并校验有效范围（避免除以0异常）
    vout_mq7 = ADC_To_Voltage(Joystick_ADC[0]);
    if (vout_mq7 <= 0.001f || vout_mq7 >= (VCC - 0.001f)) {
        gas_conc.co_concentration = -1.0f; // 标记为无效值
    } else {
        // 步骤2：计算MQ-7的传感器电阻Rs
        // Rs计算公式：Rs = (Vcc - Vout) * Rl / Vout （基于串联负载电阻电路）
        rs_mq7 = (VCC - vout_mq7) * RL / vout_mq7;

        // 步骤3：计算Rs/R0电阻比（R0为清洁空气中的基准电阻）
        rs_r0_ratio_mq7 = rs_mq7 / MQ7_R0_CLEAN_AIR;

        // 步骤4：根据对数关系计算CO浓度（核心公式，来自MQ传感器datasheet）
        // 对数关系：log10(C) = slope * log10(Rs/R0) + intercept
        // 转换为浓度：C = 10^[slope * log10(Rs/R0) + intercept]
        gas_conc.co_concentration = powf(10.0f, (MQ7_SLOPE * log10f(rs_r0_ratio_mq7)) + MQ7_INTERCEPT);
    }

    // 步骤5：转换MQ-8的ADC值为Vout，并校验有效范围
    vout_mq8 = ADC_To_Voltage(Joystick_ADC[1]);
    if (vout_mq8 <= 0.001f || vout_mq8 >= (VCC - 0.001f)) {
        gas_conc.h2_concentration = -1.0f; // 标记为无效值
    } else {
        // 步骤6：计算MQ-8的传感器电阻Rs
        rs_mq8 = (VCC - vout_mq8) * RL / vout_mq8;

        // 步骤7：计算MQ-8的Rs/R0电阻比
        rs_r0_ratio_mq8 = rs_mq8 / MQ8_R0_CLEAN_AIR;

        // 步骤8：根据对数关系计算H₂浓度
        gas_conc.h2_concentration = powf(10.0f, (MQ8_SLOPE * log10f(rs_r0_ratio_mq8)) + MQ8_INTERCEPT);
    }

    // 返回封装后的浓度结果
    return gas_conc;
}
