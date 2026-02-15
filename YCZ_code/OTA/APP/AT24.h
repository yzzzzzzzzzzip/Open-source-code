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
extern I2C_HandleTypeDef hi2c1;

HAL_StatusTypeDef AT24C64_Init(void);
HAL_StatusTypeDef AT24C64_Write_Byte(uint16_t addr, uint8_t data);
HAL_StatusTypeDef AT24C64_Write_Multi(uint16_t addr, uint8_t *buf, uint16_t len);
HAL_StatusTypeDef AT24C64_Read_Byte(uint16_t addr, uint8_t *data);
HAL_StatusTypeDef AT24C64_Read_Multi(uint16_t addr, uint8_t *buf, uint16_t len);    


#endif // AT24_H