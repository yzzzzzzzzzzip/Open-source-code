/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"       /* HAL库头文件 */
#include "tim.h"        /* 定时器驱动头文件 */
#include "usart.h"      /* 串口驱动头文件 */
#include "gpio.h"       /* GPIO驱动头文件 */

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>     /* 字符串操作函数库 */
#include <stdio.h>      /* 标准输入输出函数库 */
#include "esp8266.h"    /* ESP8266 WiFi模块驱动头文件 */
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
// 定义定时周期（示例：1000ms = 1秒发送一次，可根据需求修改）
#define MQTT_SEND_PERIOD_MS  5000
// 记录上一次发送MQTT数据的时间戳（静态变量，仅在当前作用域有效，值会持续保存）
static uint32_t g_last_mqtt_send_tick = 0;
volatile uint8_t g_uart1_busy = 0;
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);    /* 系统时钟配置函数声明 */
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* 定时器周期中断回调函数（当前为空实现，可根据需求扩展） */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{

}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */
  /* 用户初始化代码区域（通常在HAL初始化前执行） */
  
  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/
  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();   /* 初始化HAL库 */

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();   /* 配置系统时钟 */

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();          /* 初始化GPIO */
  MX_USART1_UART_Init();   /* 初始化USART1（连接ESP8266） */
  MX_USART2_UART_Init();   /* 初始化USART2（调试串口） */
  MX_TIM3_Init();          /* 初始化定时器TIM3 */
  MX_TIM2_Init();          /* 初始化定时器TIM2 */
  MX_TIM4_Init();          /* 初始化定时器TIM4 */

  /* USER CODE BEGIN 2 */
  uint8_t status = 2;  /* WiFi连接状态：2表示已连接并获取IP */
  /* ESP8266模块初始化 */
  ESP8266_Init();
  /* 配置ESP8266连接MQTT服务器（安可信固件示例） */
  ESP8266_Aithinker_MQTT_Example();
  /* 订阅OneNET平台属性设置主题 */
  ESP8266_MQTT_Subscribe_OneNET();
  /* 订阅OneNET平台属性上报回复主题 */
  ESP8266_MQTT_Subscribe_OneNET_Repaly();
  /* 绑定OneNET上报主题与命令 */
  ESP8266_Bind_OneNET_Report_Topic_With_Cmd();
  /* 记录初始时间戳 */
  g_last_mqtt_send_tick = HAL_GetTick();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* 根据OneNET平台下发的属性数据控制GPIO引脚状态 */
    /* 控制my_room引脚（根据light_b属性） */
    HAL_GPIO_WritePin(my_room_GPIO_Port, my_room_Pin, 
                      g_OneNET_Property_Data.light_b ? GPIO_PIN_RESET : GPIO_PIN_SET);
    /* 控制back引脚（根据light_back属性） */
    HAL_GPIO_WritePin(back_GPIO_Port, back_Pin, 
                      g_OneNET_Property_Data.light_back ? GPIO_PIN_SET : GPIO_PIN_RESET);
    /* 控制fawrd引脚（根据light_f属性） */
    HAL_GPIO_WritePin(fawrd_GPIO_Port, fawrd_Pin, 
                      g_OneNET_Property_Data.light_f ? GPIO_PIN_RESET : GPIO_PIN_SET);
    /* 控制sun引脚（根据sun属性） */
    HAL_GPIO_WritePin(sun_GPIO_Port, sun_Pin, 
                      g_OneNET_Property_Data.sun ? GPIO_PIN_RESET : GPIO_PIN_SET);

    /* 执行主循环任务（如数据解析、状态检查等） */
    main_loop_task();

    uint32_t current_tick = HAL_GetTick(); // 获取当前系统节拍时间戳
    
    /* 处理MQTT数据上报 */
    /* 条件1：串口1不忙（未在发送数据） */
    /* 条件2：OneNET属性数据已更新（需要上报） */
    if (g_uart1_busy == 0 && g_OneNET_Property_Data.is_updated)
    {
      g_uart1_busy = 1; // 临时占标：防止上报过程中解析突然触发
      // 执行上报，上报完成后立即释放标志
      ESP8266_AT_MQTT_Publish_Raw(g_OneNET_Property_Data.light_b,
                                  g_OneNET_Property_Data.light_back,
                                  g_OneNET_Property_Data.light_f,
                                  g_OneNET_Property_Data.sun);
      g_uart1_busy = 0; // 释放标志：上报完成，恢复解析优先
      g_OneNET_Property_Data.is_updated = 0;  /* 清除更新标志 */
      UART2_Debug_Print("MQTT Publish: Success (Parse Idle)");
    }
    
    /* 解析忙则跳过：不更新时间戳，下一个周期继续检测，直到解析空闲 */
    /* 条件1：串口1不忙 */
    /* 条件2：ESP8266数据帧未完成标志未置位（表示没有正在解析的数据） */
    /* 条件3：OneNET属性数据未更新（避免与上报冲突） */
    if (g_uart1_busy == 0 && strEsp8266_Fram_Record.InfBit.FramFinishFlag != 1 && g_OneNET_Property_Data.is_updated != 1)
    {
      /* 检查是否达到60秒（60000ms）的心跳/状态检查间隔 */
      if (current_tick - g_last_mqtt_send_tick > 60000)
      {
        /* 获取ESP8266连接状态 */
        status = ESP8266_Get_LinkStatus();
        /* 如果连接状态异常（不是已连接状态） */
        if (status != 2)
        {
          /* 重新初始化ESP8266并连接MQTT服务器 */
          ESP8266_Init();
          ESP8266_Aithinker_MQTT_Example();
          ESP8266_MQTT_Subscribe_OneNET();
          ESP8266_MQTT_Subscribe_OneNET_Repaly();
          ESP8266_Bind_OneNET_Report_Topic_With_Cmd();
        }
        g_last_mqtt_send_tick = current_tick; // 仅上报成功才更新时间戳
      }
      // 不更新g_last_mqtt_send_tick，保证上报周期连续检测
    }
    else
    {
      /* 如果串口忙或正在解析数据，短暂延时避免CPU占用过高 */
      HAL_Delay(10);
    }
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;  /* 使用外部高速时钟(HSE) */
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;                    /* 使能HSE */
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;     /* HSE预分频系数为1 */
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;                    /* 使能内部高速时钟(HSI) */
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;                /* 使能PLL */
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;        /* PLL时钟源选择HSE */
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;                /* PLL倍频系数为9 */
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();  /* 时钟配置失败，执行错误处理 */
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                                RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;  /* 配置所有时钟域 */
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;  /* 系统时钟源选择PLL输出 */
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;         /* AHB总线时钟不分频 */
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;          /* APB1总线时钟2分频 */
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;          /* APB2总线时钟不分频 */

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();  /* 时钟配置失败，执行错误处理 */
  }
}

/* USER CODE BEGIN 4 */
/* 定时器输入捕获中断回调函数（当前为空实现，可根据需求扩展） */
void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim)
{

}
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();   /* 禁用所有中断 */
  while (1)
  {
    /* 错误处理：可在此处添加错误指示灯闪烁、日志记录等 */
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
