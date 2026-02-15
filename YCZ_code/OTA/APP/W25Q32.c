#include "W25Q32.h"
#include "debug_printf.h"
#include <string.h>


// ==================== 核心：单字节用阻塞式（优雅），多字节用DMA ====================
// 单字节阻塞发送（指令用，简洁高效）
static void SPI1_WriteByte(uint8_t TxData)
{
    HAL_SPI_Transmit(&hspi1, &TxData, 1, 10);
}

// 单字节阻塞接收（指令响应用）
static uint8_t SPI1_ReadByte(void)
{
    uint8_t RxData = 0;
    HAL_SPI_Receive(&hspi1, &RxData, 1, 10);
    return RxData;
}

// DMA传输完成标志（仅多字节用）
static volatile uint8_t spi_dma_tx_done = 0;
static volatile uint8_t spi_dma_rx_done = 0;

// ==================== DMA回调函数（仅多字节用） ====================
void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance == SPI1) spi_dma_tx_done = 1;
}

void HAL_SPI_RxCpltCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance == SPI1) spi_dma_rx_done = 1;
}

// ==================== 多字节DMA传输（批量读写用，优雅封装） ====================
// DMA批量发送（返回是否成功）
static uint8_t SPI1_DMA_Transmit(uint8_t *pData, uint16_t len)
{
    spi_dma_tx_done = 0;
    if (HAL_SPI_Transmit_DMA(&hspi1, pData, len) != HAL_OK) return 1;
    while (!spi_dma_tx_done); // 仅多字节等待，逻辑更聚焦
    return 0;
}

// DMA批量接收
static uint8_t SPI1_DMA_Receive(uint8_t *pData, uint16_t len)
{
    spi_dma_rx_done = 0;
    if (HAL_SPI_Receive_DMA(&hspi1, pData, len) != HAL_OK) return 1;
    while (!spi_dma_rx_done);
    return 0;
}

// ==================== 业务函数（优雅混合） ====================
// 写使能（单字节阻塞，简洁）
void SPI_FLASH_Write_Enable(void)   
{
    SPI_FLASH_CS_L();
    SPI1_WriteByte(W25X_WriteEnable);
    SPI_FLASH_CS_H();
    HAL_Delay(1);
}

// 读状态寄存器（单字节阻塞）
uint8_t SPI_Flash_ReadSR(void)   
{  
    uint8_t byte=0;   
    SPI_FLASH_CS_L();
    SPI1_WriteByte(W25X_ReadStatusReg);
    byte = SPI1_ReadByte();
    SPI_FLASH_CS_H();
    return byte;
}

// 读ID（单字节阻塞，保留原有简洁逻辑）
uint16_t SPI_Flash_ReadID(void)
{
    uint16_t Temp = 0;	  
    SPI_FLASH_CS_L();
    SPI1_WriteByte(0x90);	    
    SPI1_WriteByte(0x00); 	    
    SPI1_WriteByte(0x00); 	    
    SPI1_WriteByte(0x00); 	 
    Temp |= SPI1_ReadByte() << 8;  
    Temp |= SPI1_ReadByte();	 
    SPI_FLASH_CS_H();
    printf("SPI Flash ID: 0x%04X\r\n", Temp);
    return Temp;
}

// 扇区擦除（单字节指令，阻塞）
void SPI_Flash_Erase_Sector(uint32_t Dst_Addr)
{
    Dst_Addr &= 0xFFFFF000;
    SPI_FLASH_Write_Enable();
    SPI_Flash_Wait_Busy();
    
    SPI_FLASH_CS_L();
    SPI1_WriteByte(W25X_SectorErase);
    SPI1_WriteByte((uint8_t)(Dst_Addr>>16));
    SPI1_WriteByte((uint8_t)(Dst_Addr>>8));
    SPI1_WriteByte((uint8_t)Dst_Addr);
    SPI_FLASH_CS_H();
    
    SPI_Flash_Wait_Busy();
}

// 多字节读取（DMA批量，优雅高效）
void SPI_Flash_Read(uint32_t ReadAddr, uint16_t NumByteToRead, uint8_t* pBuffer)   
{ 
    SPI_FLASH_CS_L();                         
    // 指令+地址：单字节阻塞（短指令，无需DMA）
    SPI1_WriteByte(W25X_ReadData);         
    SPI1_WriteByte((uint8_t)(ReadAddr>>16));
    SPI1_WriteByte((uint8_t)(ReadAddr>>8));
    SPI1_WriteByte((uint8_t)ReadAddr);
    // 数据：DMA批量（核心优化点）
    SPI1_DMA_Receive(pBuffer, NumByteToRead);
    SPI_FLASH_CS_H();
}

// 多字节写页（DMA批量）
void SPI_Flash_Write_Page(uint32_t WriteAddr, uint16_t NumByteToWrite, uint8_t* pBuffer)
{
    SPI_FLASH_Write_Enable();
    SPI_Flash_Wait_Busy();
    
    SPI_FLASH_CS_L();
    // 指令+地址：单字节阻塞
    SPI1_WriteByte(W25X_PageProgram);      
    SPI1_WriteByte((uint8_t)(WriteAddr>>16));
    SPI1_WriteByte((uint8_t)(WriteAddr>>8));
    SPI1_WriteByte((uint8_t)WriteAddr);
    // 数据：DMA批量
    SPI1_DMA_Transmit(pBuffer, NumByteToWrite);
    SPI_FLASH_CS_H();
    SPI_Flash_Wait_Busy();
}

// 等待空闲（DMA版，逻辑不变）
void SPI_Flash_Wait_Busy(void)   
{   
    while ((SPI_Flash_ReadSR()&0x01)==0x01);   // 等待BUSY位清空
} 


// 测试函数（逻辑不变，更优雅）
uint8_t W25Q64_Test_ReadWrite(void)
{
    uint32_t test_addr = 0x000010;       
    uint8_t write_buf[] = "hello w25q64";
    uint16_t write_len = sizeof(write_buf); 
    uint8_t read_buf[32] = {0};          
    
    printf("============ W25Q64 混合模式读写测试 ============\r\n");
    printf("测试地址：0x%06X，数据：%s（%d字节）\r\n", test_addr, write_buf, write_len);

    SPI_Flash_Erase_Sector(test_addr);
    SPI_Flash_Write_Page(test_addr, write_len, write_buf);
    memset(read_buf, 0, sizeof(read_buf));
    SPI_Flash_Read(test_addr, write_len, read_buf);

    if (memcmp(write_buf, read_buf, write_len) == 0)
    {
        printf("✅ 测试成功！读取到：%s\r\n", read_buf);
    }
    else
    {
        printf("❌ 测试失败！\r\n");
        return 1;
    }
    return 0;
}