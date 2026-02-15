#ifndef MQ_GAS_SENSOR_H
#define MQ_GAS_SENSOR_H

/************************ 头文件包含 ************************/
#include <stdint.h>

/************************ 宏定义（根据实际硬件调整） ************************/
// ADC配置参数（STM32常用12位ADC，可根据实际分辨率修改）
#define ADC_RESOLUTION    4095.0f  // 12位ADC最大数值（2^12-1）
#define VREF              3.3f     // ADC参考电压（单位：V，通常3.3V或5.0V）
#define VCC               5.0f     // 传感器供电电压（单位：V，MQ系列多为5V供电）
#define RL                10000.0f // 串联负载电阻阻值（单位：Ω，推荐10KΩ，根据实际焊接值修改）

// MQ-7（CO）特性参数（参考datasheet典型值，需实际标定优化）
#define MQ7_R0_CLEAN_AIR  10000.0f // 清洁空气中的传感器基准电阻（Ω，需校准获取更准确值）
#define MQ7_SLOPE         -0.35f   // 浓度-电阻比对数曲线斜率（datasheet典型值）
#define MQ7_INTERCEPT     0.8f     // 浓度-电阻比对数曲线截距（datasheet典型值）

// MQ-8（H₂）特性参数（参考datasheet典型值，需实际标定优化）
#define MQ8_R0_CLEAN_AIR  5000.0f  // 清洁空气中的传感器基准电阻（Ω，需校准获取更准确值）
#define MQ8_SLOPE         -0.40f   // 浓度-电阻比对数曲线斜率（datasheet典型值）
#define MQ8_INTERCEPT     1.0f     // 浓度-电阻比对数曲线截距（datasheet典型值）

/************************ 数据结构定义（用于返回浓度结果） ************************/
// 气体浓度结果结构体，封装MQ-7和MQ-8的计算结果
typedef struct {
    float co_concentration;   // MQ-7检测的CO浓度（单位：ppm，无效值标记为-1.0f）
    float h2_concentration;   // MQ-8检测的H₂浓度（单位：ppm，无效值标记为-1.0f）
} GasConcentration_t;

/************************ 核心函数声明（对外暴露的接口） ************************/
/**
 * @brief  输入Joystick_ADC数组，计算CO和H₂的气体浓度
 * @param  Joystick_ADC: 数组，[0]=MQ-7 ADC值，[1]=MQ-8 ADC值
 * @retval GasConcentration_t: 封装后的浓度结果结构体
 */
GasConcentration_t Calculate_Gas_Concentration(uint16_t Joystick_ADC[2]);

#endif /* MQ_GAS_SENSOR_H */
