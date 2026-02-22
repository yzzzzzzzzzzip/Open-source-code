#ifndef AT24_H
#define AT24_H
#include "main.h"


// AT24C64 配置
#define AT24C64_DEV_ADDR     0xA0    // 7位地址0x50 << 1（AT24C64 A0/A1/A2无效）
#define AT24C64_ADDR_SIZE    I2C_MEMADD_SIZE_16BIT // 2字节地址
#define AT24C64_PAGE_SIZE    32      // 页大小32Byte
#define AT24C64_MAX_ADDR     8191    // 最大地址8191
#define I2C_TIMEOUT          1000    // 通信超时时间(ms)
#define AT24C64_INIT_RETRY   3       // 初始化重试次数（提高鲁棒性）

// 在Bootloader开头添加这些宏定义
#define AT24C64_OTA_FLAG_ADDR    0x0000  // OTA标志位存储在AT24C64的起始地址
#define FIRMWARE_SIZE_ADDR   0x0004  // 存储固件大小的地址
#define OTA_FLAG_NEED_UPDATE     0xAA    // 需要更新的标志
#define OTA_FLAG_NO_UPDATE        0x00    // 不需要更新的标志


extern I2C_HandleTypeDef hi2c1;

HAL_StatusTypeDef AT24C64_Init(void);
HAL_StatusTypeDef AT24C64_Write_Byte(uint16_t addr, uint8_t data);
HAL_StatusTypeDef AT24C64_Write_Multi(uint16_t addr, uint8_t *buf, uint16_t len);
HAL_StatusTypeDef AT24C64_Read_Byte(uint16_t addr, uint8_t *data);
HAL_StatusTypeDef AT24C64_Read_Multi(uint16_t addr, uint8_t *buf, uint16_t len);    

HAL_StatusTypeDef AT24C64_Store_Firmware_Size(uint32_t firmware_size);
HAL_StatusTypeDef AT24C64_Read_Firmware_Size(uint32_t *firmware_size);
void AT24C64_Demo_Test_Firmware_Size(void);
#endif // AT24_H