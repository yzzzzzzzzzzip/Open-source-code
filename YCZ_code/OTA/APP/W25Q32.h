#ifndef W25Q32_H
#define W25Q32_H
#include "main.h"
//#include "dma.h"
#include "spi.h"
#include "stm32f1xx_hal_spi.h" 
#include "stdio.h"
#include "string.h"
// 前置声明（根据MCU型号调整）
extern SPI_HandleTypeDef hspi1;

// ==================== 1. 指令定义 ====================
#define W25X_WriteEnable        0x06  // 写使能
#define W25X_WriteDisable       0x04  // 写禁止
#define W25X_ReadStatusReg      0x05  // 读状态寄存器
#define W25X_ReadData           0x03  // 读数据
#define W25X_PageProgram        0x02  // 页编程
#define W25X_SectorErase        0x20  // 扇区擦除(4KB)
#define W25X_ManufactDeviceID   0x90  // 读厂商/设备ID

// ==================== 2. CS引脚定义（根据硬件修改） ====================
#define SPI_FLASH_CS_GPIO_PORT  GPIOA
#define SPI_FLASH_CS_PIN        GPIO_PIN_4
#define SPI_FLASH_CS_L()        HAL_GPIO_WritePin(SPI_FLASH_CS_GPIO_PORT, SPI_FLASH_CS_PIN, GPIO_PIN_RESET)
#define SPI_FLASH_CS_H()        HAL_GPIO_WritePin(SPI_FLASH_CS_GPIO_PORT, SPI_FLASH_CS_PIN, GPIO_PIN_SET)

// ==================== 核心：单字节用阻塞式（优雅），多字节用DMA ====================
// ==================== DMA回调函数（仅多字节用） ====================
void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi);
void HAL_SPI_RxCpltCallback(SPI_HandleTypeDef *hspi);
// ==================== 多字节DMA传输（批量读写用，优雅封装） ====================
// DMA批量发送（返回是否成功）
static uint8_t SPI1_DMA_Transmit(uint8_t *pData, uint16_t len);
// DMA批量接收
static uint8_t SPI1_DMA_Receive(uint8_t *pData, uint16_t len);
// ==================== 业务函数（优雅混合） ====================
// 写使能（单字节阻塞，简洁）
void SPI_FLASH_Write_Enable(void);   
// 读状态寄存器（单字节阻塞）
uint8_t SPI_Flash_ReadSR(void);   
// 读ID（单字节阻塞，保留原有简洁逻辑）
uint16_t SPI_Flash_ReadID(void);
// 扇区擦除（单字节指令，阻塞）
void SPI_Flash_Erase_Sector(uint32_t Dst_Addr);
// 多字节读取（DMA批量，优雅高效）
void SPI_Flash_Read(uint32_t ReadAddr, uint16_t NumByteToRead, uint8_t* pBuffer);   
// 多字节写页（DMA批量）
void SPI_Flash_Write_Page(uint32_t WriteAddr, uint16_t NumByteToWrite, uint8_t* pBuffer);
// 测试函数（逻辑不变，更优雅）
uint8_t W25Q64_Test_ReadWrite(void);
void SPI_Flash_Wait_Busy(void);  
#endif // W25Q32_H