#include "debug_printf.h"
#include <stdint.h>
#include "AT24.h"

/**
 * @brief  AT24C64初始化函数（含I2C外设初始化+芯片通信检测）
 * @note   1. 先初始化STM32硬件I2C外设
 *         2. 检测AT24C64是否存在（发送I2C起始信号验证）
 *         3. 支持重试机制，避免单次检测失败误判
 * @param  None
 * @retval HAL_StatusTypeDef: HAL_OK=初始化成功；HAL_ERROR=初始化失败
 */
HAL_StatusTypeDef AT24C64_Init(void)
{
  HAL_StatusTypeDef init_status = HAL_ERROR;
  uint8_t retry_cnt = 0; // 重试计数器

  // 步骤1：检测AT24C64通信（发送I2C起始信号+设备地址，验证ACK）
  // 重试机制：最多重试AT24C64_INIT_RETRY次
  while (retry_cnt < AT24C64_INIT_RETRY)
  {
    // HAL_I2C_IsDeviceReady：检测I2C设备是否就绪（发送地址+等待ACK）
    init_status = HAL_I2C_IsDeviceReady(&hi2c1, AT24C64_DEV_ADDR, 3, I2C_TIMEOUT);
    
    if (init_status == HAL_OK)
    {
      // 检测成功，跳出重试循环
      break;
    }
    
    // 检测失败，延时5ms后重试
    HAL_Delay(5);
    retry_cnt++;
  }

  // 步骤2：返回初始化状态，并打印调试信息（如果已重定向printf）
  if (init_status == HAL_OK)
  {
    printf("AT24C64 初始化成功！\r\n");
  }
  else
  {
    printf("AT24C64 初始化失败（重试%d次后仍未检测到芯片）！\r\n", AT24C64_INIT_RETRY);
  }

  return init_status;
}

/**
 * @brief  AT24C64 单字节写入
 * @param  addr: 写入地址（0~8191）
 * @param  data: 要写入的字节
 * @retval HAL_StatusTypeDef
 */
HAL_StatusTypeDef AT24C64_Write_Byte(uint16_t addr, uint8_t data)
{
  HAL_Delay(5);  // 等待内部擦除
  return HAL_I2C_Mem_Write(&hi2c1, AT24C64_DEV_ADDR, addr, 
                          AT24C64_ADDR_SIZE, &data, 1, I2C_TIMEOUT);
}

/**
 * @brief  AT24C64 多字节写入（处理32Byte页边界）
 * @param  addr: 起始地址
 * @param  buf: 写入缓冲区
 * @param  len: 写入长度
 * @retval HAL_StatusTypeDef
 */
HAL_StatusTypeDef AT24C64_Write_Multi(uint16_t addr, uint8_t *buf, uint16_t len)
{
  uint16_t remain_len = len;
  uint16_t cur_addr = addr;
  uint16_t write_len = 0;

  while (remain_len > 0)
  {
    // 计算当前页剩余可写长度
    write_len = AT24C64_PAGE_SIZE - (cur_addr % AT24C64_PAGE_SIZE);
    if (write_len > remain_len) write_len = remain_len;

    if (HAL_I2C_Mem_Write(&hi2c1, AT24C64_DEV_ADDR, cur_addr, 
                          AT24C64_ADDR_SIZE, buf + (addr - cur_addr), 
                          write_len, I2C_TIMEOUT) != HAL_OK)
    {
      return HAL_ERROR;
    }

    HAL_Delay(5);
    remain_len -= write_len;
    cur_addr += write_len;
  }
  return HAL_OK;
}

/**
 * @brief  AT24C64 单字节读取
 * @param  addr: 读取地址（0~8191）
 * @param  data: 接收指针
 * @retval HAL_StatusTypeDef
 */
HAL_StatusTypeDef AT24C64_Read_Byte(uint16_t addr, uint8_t *data)
{
  return HAL_I2C_Mem_Read(&hi2c1, AT24C64_DEV_ADDR, addr, 
                         AT24C64_ADDR_SIZE, data, 1, I2C_TIMEOUT);
}

/**
 * @brief  AT24C64 多字节读取
 * @param  addr: 起始地址
 * @param  buf: 接收缓冲区
 * @param  len: 读取长度
 * @retval HAL_StatusTypeDef
 */
HAL_StatusTypeDef AT24C64_Read_Multi(uint16_t addr, uint8_t *buf, uint16_t len)
{
  return HAL_I2C_Mem_Read(&hi2c1, AT24C64_DEV_ADDR, addr, 
                         AT24C64_ADDR_SIZE, buf, len, I2C_TIMEOUT);
}

