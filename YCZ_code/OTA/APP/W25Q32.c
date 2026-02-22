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
/**
 * @brief 读取并打印W25Q32中指定地址的固件数据（十六进制）
 * @param start_addr 读取起始地址（需与OTA_FLASH_START_ADDR对应，如0x000000）
 * @param len        读取长度（建议256的倍数，如256、512等）
 */
void W25Q32_Print_OTA_Data(uint32_t start_addr, uint16_t len)
{
    if (len == 0)
    {
        printf("【W25Q32】读取长度不能为0！");
        return;
    }

    // 分配读取缓冲区（最大一次读512字节，避免栈溢出）
    uint8_t read_buf[512] = {0};
    if (len > sizeof(read_buf))
    {
        printf("【W25Q32】单次读取长度不能超过512字节！");
        return;
    }

    // 从Flash读取数据
    SPI_Flash_Read(start_addr, len, read_buf);
    printf("【W25Q32】读取地址：0x%06X，长度：%d 字节，数据：", start_addr, len);

    // 按16字节一行打印（和之前固件打印格式一致）
    char temp_hex_buf[64] = {0};
    for (uint16_t i = 0; i < len; i++)
    {
        if (i % 16 == 0)
        {
            memset(temp_hex_buf, 0, sizeof(temp_hex_buf));
            snprintf(temp_hex_buf, sizeof(temp_hex_buf)-1, "0x%04X: ", start_addr + i);
        }

        uint16_t remain = sizeof(temp_hex_buf) - strlen(temp_hex_buf) - 1;
        if (remain >= 3)
        {
            snprintf(&temp_hex_buf[strlen(temp_hex_buf)], remain, "%02X ", read_buf[i]);
        }

        if ((i % 16 == 15) || (i == len - 1))
        {
            printf("%s", temp_hex_buf);
        }
    }
    printf("【W25Q32】数据打印完成！");
}
/**
 * @brief 批量读取并打印W25Q32中OTA固件数据（支持大长度分块打印）
 * @param start_addr 读取起始地址（如OTA_FLASH_START_ADDR=0x000000）
 * @param total_len  读取总长度（如固件总大小7360字节）
 * @param per_line   每行打印字节数（建议16，和之前格式一致）
 * @param per_chunk  每次打印块大小（建议512，避免串口刷屏）
 */
void W25Q32_Print_OTA_All_Data(uint32_t start_addr, uint32_t total_len, uint16_t per_line, uint16_t per_chunk)
{
    // 1. 入参校验
    if (total_len == 0)
    {
        printf("【W25Q32】读取长度不能为0！");
        return;
    }
    if (per_line == 0) per_line = 16;    // 默认每行16字节
    if (per_chunk == 0) per_chunk = 512; // 默认每块512字节

    printf("=====================================");
    printf("【W25Q32】开始读取数据：起始地址0x%06X，总长度%d字节", start_addr, total_len);
    printf("=====================================");

    // 2. 分配读取缓冲区（栈区安全大小）
    uint8_t read_buf[1024] = {0}; // 最大单次读1024字节
    uint32_t read_offset = 0;     // 已读取的偏移量

    // 3. 分块读取+打印
    while (read_offset < total_len)
    {
        // 计算当前块的读取长度（不超过缓冲区/剩余长度）
        uint16_t current_read_len = (total_len - read_offset) > sizeof(read_buf) ? 
                                    sizeof(read_buf) : (total_len - read_offset);
        // 限制为per_chunk的整数倍（避免频繁打印）
        current_read_len = (current_read_len / per_chunk) * per_chunk;
        if (current_read_len == 0) current_read_len = (uint16_t)(total_len - read_offset);

        // 4. 读取当前块数据
        uint32_t current_addr = start_addr + read_offset;
        SPI_Flash_Read(current_addr, current_read_len, read_buf);
        printf("【W25Q32】块%d：地址0x%06X - 0x%06X，长度%d字节",
                          (read_offset / per_chunk) + 1,
                          current_addr, current_addr + current_read_len - 1,
                          current_read_len);

        // 5. 按行打印当前块数据（格式和固件下载一致）
        char temp_hex_buf[128] = {0};
        for (uint16_t i = 0; i < current_read_len; i++)
        {
            // 每行开头打印地址+重置缓冲区
            if (i % per_line == 0)
            {
                memset(temp_hex_buf, 0, sizeof(temp_hex_buf));
                snprintf(temp_hex_buf, sizeof(temp_hex_buf)-1, "0x%04X: ", current_addr + i);
            }

            // 拼接十六进制字节（XX 格式）
            uint16_t remain = sizeof(temp_hex_buf) - strlen(temp_hex_buf) - 1;
            if (remain >= 3)
            {
                snprintf(&temp_hex_buf[strlen(temp_hex_buf)], remain, "%02X ", read_buf[i]);
            }

            // 每行结束/最后一行打印
            if ((i % per_line == per_line - 1) || (i == current_read_len - 1))
            {
                printf("%s", temp_hex_buf);
            }
        }

        // 6. 更新偏移量+短暂延时（避免串口溢出）
        read_offset += current_read_len;
        HAL_Delay(100); // 每打印一块延时100ms
    }

    printf("=====================================");
    printf("【W25Q32】全部数据读取打印完成！总计%d字节", total_len);
    printf("=====================================");
}

// 简化版调用宏（适配你的OTA场景）
