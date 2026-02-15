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
#include "main.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>
#include <stdio.h>
#include "esp8266.h"

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
volatile	uint8_t g_uart1_busy = 0;

	
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
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

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART1_UART_Init();
  MX_USART2_UART_Init();
  MX_TIM3_Init();
  MX_TIM2_Init();
  MX_TIM4_Init();
  /* USER CODE BEGIN 2 */
	uint8_t status = 2;
		
		//ESP8266_Get_LinkStatus();
		//ESP8266_ExitUnvarnishSend();
		
		ESP8266_Init();
		ESP8266_Aithinker_MQTT_Example();
		ESP8266_MQTT_Subscribe_OneNET();
		ESP8266_MQTT_Subscribe_OneNET_Repaly();
		ESP8266_Bind_OneNET_Report_Topic_With_Cmd();

		//ESP8266_Enter_Transparent_Mode();
		

			 g_last_mqtt_send_tick = HAL_GetTick();
		
					
		//ESP8266_Link_Server(NET_PROTOCOL_MQTT,"mqtts.heclouds.com","1883",ID_NO_SINGLE);
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
		
					// 1. 控制light_b引脚
				HAL_GPIO_WritePin(my_room_GPIO_Port, my_room_Pin, 
													g_OneNET_Property_Data.light_b ? GPIO_PIN_RESET : GPIO_PIN_SET);
				// 2. 控制light_back引脚
				HAL_GPIO_WritePin(back_GPIO_Port, back_Pin, 
													g_OneNET_Property_Data.light_back ? GPIO_PIN_SET : GPIO_PIN_RESET);
				// 3. 控制light_f引脚
				HAL_GPIO_WritePin(fawrd_GPIO_Port, fawrd_Pin, 
													g_OneNET_Property_Data.light_f ? GPIO_PIN_RESET : GPIO_PIN_SET);
				// 4. 控制sun引脚
				HAL_GPIO_WritePin(sun_GPIO_Port, sun_Pin, 
												g_OneNET_Property_Data.sun ? GPIO_PIN_RESET : GPIO_PIN_SET);
				
				main_loop_task();
		
				uint32_t current_tick = HAL_GetTick(); // 获取当前系统节拍时间戳
				if (g_uart1_busy == 0 &&g_OneNET_Property_Data.is_updated)
        {
            g_uart1_busy = 1; // 临时占标：防止上报过程中解析突然触发
            // 执行上报，上报完成后立即释放标志
            ESP8266_AT_MQTT_Publish_Raw(g_OneNET_Property_Data.light_b,
                                        g_OneNET_Property_Data.light_back,
                                        g_OneNET_Property_Data.light_f,
                                        g_OneNET_Property_Data.sun);
            g_uart1_busy = 0; // 释放标志：上报完成，恢复解析优先
            
						g_OneNET_Property_Data.is_updated = 0;
            UART2_Debug_Print("MQTT Publish: Success (Parse Idle)");
        }
        // 解析忙则跳过：不更新时间戳，下一个周期继续检测，直到解析空闲
        if(g_uart1_busy==0&&strEsp8266_Fram_Record.InfBit.FramFinishFlag != 1&&g_OneNET_Property_Data.is_updated!=1)
        {
            if(current_tick-g_last_mqtt_send_tick>60000){
						status = ESP8266_Get_LinkStatus();
				if(status!=2)
				{
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
				HAL_Delay(10);
				
				
			
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
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

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
  __disable_irq();
  while (1)
  {
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
