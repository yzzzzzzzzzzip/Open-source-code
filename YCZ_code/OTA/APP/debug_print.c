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

