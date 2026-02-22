#include "debug_printf.h"
#include "stm32f103xb.h"
#include "stm32f1xx_hal_uart.h"
#include "usart.h"


/**
 * @brief 重写fputc函数（HAL库通用）
 * @param ch 要发送的单个字符（ASCII码）
 * @param f  文件指针（标准库参数，HAL库中忽略）
 * @retval 发送成功的字符（符合函数规范，必须返回）
 */
int fputc(int ch, FILE *f)
{
  // 1. 等待串口发送缓冲区为空（确保上一个字符发送完成）
  while (__HAL_UART_GET_FLAG(&huart2, UART_FLAG_TXE) == RESET);
  
  // 2. 发送单个字符到USART2（强制转换为uint8_t，匹配串口发送格式）
  HAL_UART_Transmit(&huart2, (uint8_t*)&ch, 1, HAL_MAX_DELAY);
  
  // 3. 返回发送的字符，保证函数调用链完整
  return ch;
}

void debug_print(const char* format, ...)
{


}

/*void UART2_Print(const char *fmt, ...)
{
    // 1. 合法性检查：格式化字符串不能为空，避免空指针操作
    if (fmt == NULL)
    {
        return;
    }

    // 2. 清空串口2发送缓冲区（避免上一次打印数据残留，导致乱码）
    memset(uart2_tx_buffer, 0, UART2_TX_BUF_MAX_LEN);

    // 3. 处理可变参数（实现格式化输出，替代 snprintf，支持多参数）
    va_list args;          // 定义可变参数列表变量
    va_start(args, fmt);   // 初始化可变参数列表，指向 fmt 后的第一个参数
    // 格式化填充缓冲区：vsnprintf 支持可变参数，更适合封装格式化函数
    vsnprintf(uart2_tx_buffer, UART2_TX_BUF_MAX_LEN - 1, fmt, args);  // 预留1字节防止溢出
    va_end(args);          // 结束可变参数列表处理，释放资源

    // 4. 自动添加换行符 \r\n（符合串口调试习惯，无需手动传入）
    strcat(uart2_tx_buffer, "\r\n");

    // 5. HAL 库标准阻塞发送，打印到串口2
    HAL_UART_Transmit(&huart2, (uint8_t*)uart2_tx_buffer, strlen(uart2_tx_buffer), UART_TIMEOUT_MS);
}*/