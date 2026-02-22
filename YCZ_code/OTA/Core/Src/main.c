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
#include "dma.h"
#include "i2c.h"
#include "spi.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "debug_printf.h"
#include <stdio.h>
#include "AT24.h"
#include "W25Q32.h"
#include "esp8266.h"
#include "OTA.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/************************ ������ַ�궨�� ************************/
#define FLASH_APP_ADDR         0x08007800
#define FLASH_APP_SIZE         0x8800

/************************ ��ת����ָ�� ************************/
typedef void (*pFunction)(void);
pFunction JumpToApplication;

/************************ ���������APP�Ƿ���Ч ************************/
static uint8_t Check_APP_Valid(void) {
    // ��ȡAPP��ջ����ַ��APP��ʼ��ַ�ĵ�һ���֣�
    uint32_t app_stack_top = *(volatile uint32_t*)FLASH_APP_ADDR;
    // STM32F103C8T6��SRAM��Χ�� 0x20000000 - 0x20005000
    // ���ջ����ַ�������Χ�ڣ�˵��APP����Ч��
    if ((app_stack_top >= 0x20000000) && (app_stack_top <= 0x20005000)) {
        return 1; // APP��Ч
    }
    return 0; // APP��Ч
}
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/************************ ��ת��APP ************************/
static void Jump_To_APP(void) {
    printf("[Bootloader] Jump to APP...\r\n");
    
    __disable_irq();
    
    // �ر�����
    HAL_SPI_DeInit(&hspi1);
    HAL_I2C_DeInit(&hi2c1);
    HAL_UART_DeInit(&huart1);
    
    // ����ջָ��
    uint32_t app_stack_top = *(volatile uint32_t*)FLASH_APP_ADDR;
    __set_MSP(app_stack_top);
    
    // ��ȡ��λ�ж�����
    uint32_t app_reset_handler = *(volatile uint32_t*)(FLASH_APP_ADDR + 4);
    JumpToApplication = (pFunction)app_reset_handler;
    
    __enable_irq();
    JumpToApplication();
    
    while(1);
}

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

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
  // ���� ��������ͷ����HAL_Init()֮ǰ������
  // �ж�������ƫ�Ƶ�APP��ʼ��ַ
  //SCB->VTOR = FLASH_APP_ADDR; // 0x08007800
	 
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
  MX_DMA_Init();
  MX_I2C1_Init();
  MX_SPI1_Init();
  MX_USART2_UART_Init();
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
  HAL_Delay(2000);
  
	printf("\r\n============ Bootloader Start ============\r\n");
	SPI_Flash_ReadID();
	AT24C64_Init();
	printf("\n2. AT24C64...\n");
	uint32_t firmware_size_=428506;
	AT24C64_Store_Firmware_Size(firmware_size_);
	printf("\n3. READ...\n");
	
	uint32_t firmware_size_read_immediate = 0;
	AT24C64_Read_Firmware_Size(&firmware_size_read_immediate);
if (firmware_size_read_immediate == firmware_size_)
	        printf("size: %u \n", 
               firmware_size_read_immediate);
	/*
	ESP8266_ExitUnvarnishSend();
	HAL_Delay(1000);
	ESP8266_Rst();
	HAL_Delay(1000);
	ESP8266_AT_Test();	
	ESP8266_StaTcpClient(); 
	ESP8266_OTACheckVersion();
  //OTA_Full_Process(g_ota_firmware_info.tid,g_ota_firmware_info.size);
	//ESP8266_OTA_Download_All(g_ota_firmware_info.tid,g_ota_firmware_info.size,3);
	OTA_Full_Process(g_ota_firmware_info.tid,g_ota_firmware_info.size);
 // W25Q32_Print_OTA_Firmware(7360);
*/
	
	//HAL_Delay(5000);
	//ESP8266_ExitUnvarnishSend();
	//HAL_Delay(100);
	//ESP8266_Rst();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
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
