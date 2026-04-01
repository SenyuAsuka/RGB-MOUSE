/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2024 STMicroelectronics.
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
#include "i2c.h"
#include "spi.h"
#include "tim.h"
#include "usb_device.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "usbd_hid.h"
#include "delay.h"
#include "PAW3395.h"
#include "myMouse.h"
#include "OLED.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

#define TASKNUM_MAX	5  // 修改为5，新增RGB任务

typedef struct{
	void (*pTask)(void);    //任务函数
	uint16_t TaskPeriod;    //倒计时频率
}TaskStruct;

typedef enum KEY{
	Left_Key = 1,
	Right_Key,
	Middel_Key,
	DPI_Key
}KEY;

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
uint8_t Key_Read(void);
void Task_Init(void);
void Task_Run(void);
void Key_Task(void);
void Mouse_XY_Updata(void);
void Mouse_wheel_Updata(void);
void Show_Task(void);
void LED_Task(void);
void RGB_Task(void); // 新增RGB炫彩任务声明
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
uint8_t Key_Value;
uint8_t Key_Down;
uint8_t Key_UP;
uint8_t Key_Old;

uint8_t Left_Key_Value = 0;
uint8_t Right_Key_Value = 0;
uint8_t Middel_Key_Value = 0;

int8_t wheel_num;

struct mouseHID_t mouseHID;
uint8_t motion_burst_data[12] = {0};
int16_t X_Speed,Y_Speed;
uint16_t DPI = 1500;    //初始DPI数值

uint32_t SYS_tick_ms;

uint8_t led_flag = 0;
uint8_t Key_cnt = 0;

// --- WS2812B 相关变量配置 ---
#define LED_NUM 8 
uint16_t RGB_DMA_Buffer[(24 * LED_NUM) + 50] = {0}; // DMA发送缓冲区
uint8_t rgb_offset = 0; // 颜色偏移量，用于实现流动效果

uint16_t TaskTimer[TASKNUM_MAX];
TaskStruct Task[] = {
	{Key_Task, 20},
	{Mouse_XY_Updata, 1},
	{Mouse_wheel_Updata, 5},
    {LED_Task, 100},
    {RGB_Task, 30}  // 每30ms刷新一次灯光颜色
};

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/****************************** 定时器中断回调函数 *********************************/
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *hitm)
{
	uint8_t i;
	
    // 系统毫秒计数
	SYS_tick_ms++;
	
	for(i = 0; i < TASKNUM_MAX; i++){
		if(TaskTimer[i])
			TaskTimer[i]--;
	}	
}

/****************************** 任务调度初始化 *********************************/
void Task_Init(void)
{
	uint8_t NTask;
	for(NTask = 0; NTask < sizeof(Task)/sizeof(Task[0]); NTask++){
		TaskTimer[NTask] = Task[NTask].TaskPeriod;
	}
}	

void Task_Run(void)
{
	uint8_t NTask;
	for(NTask = 0; NTask < sizeof(Task)/sizeof(Task[0]); NTask++){
		if(TaskTimer[NTask] == 0)
		{
			TaskTimer[NTask] = Task[NTask].TaskPeriod;
			(Task[NTask].pTask)();
		}
	}
}

/****************************** WS2812B 炫彩逻辑 *********************************/

// 彩虹颜色生成算法：输入0-255，输出对应RGB颜色
uint32_t Wheel(uint8_t WheelPos) {
    WheelPos = 255 - WheelPos;
    if(WheelPos < 85) {
        return ((255 - WheelPos * 3) << 16) | (0 << 8) | (WheelPos * 3);
    }
    if(WheelPos < 170) {
        WheelPos -= 85;
        return (0 << 16) | ((WheelPos * 3) << 8) | (255 - WheelPos * 3);
    }
    WheelPos -= 170;
    return ((WheelPos * 3) << 16) | ((255 - WheelPos * 3) << 8) | 0;
}

// RGB 炫彩刷新任务
void RGB_Task(void) {
    uint32_t color;
    uint8_t r, g, b;
    uint16_t idx = 0;
    
    // 基于 72MHz, ARR=89 的 PWM 占空比定义
    const uint16_t WS_HIGH = 60; 
    const uint16_t WS_LOW  = 28;
    
    for(int i = 0; i < LED_NUM; i++) {
        // 计算每个灯珠的颜色，i*32 产生颜色差，形成流动感
        color = Wheel((rgb_offset + i * 32) & 255); 
        
        // 亮度缩减：除以 5，避免电流过大导致鼠标掉线
        g = ((color >> 16) & 0xFF) / 5;
        r = ((color >> 8)  & 0xFF) / 5;
        b = (color & 0xFF) / 5;
        
        uint32_t grb = (g << 16) | (r << 8) | b;
        
        // 将 24位颜色转为 PWM 占空比填入缓冲区
        for (int j = 23; j >= 0; j--) {
            RGB_DMA_Buffer[idx++] = (grb & (1 << j)) ? WS_HIGH : WS_LOW;
        }
    }
    
    // 启动 DMA 发送 (PB6 对应 TIM4 CH1)
    HAL_TIM_PWM_Start_DMA(&htim4, TIM_CHANNEL_1, (uint32_t *)RGB_DMA_Buffer, (24 * LED_NUM) + 50);
    
    // 更新偏移，控制流水速度
    rgb_offset += 3; 
}

/****************************** 鼠标业务逻辑 *********************************/
// 按键处理任务
void Key_Task(void)
{
	Key_Value = Key_Read();
	Key_Down = Key_Value&(Key_Old^Key_Value);
	Key_UP = ~Key_Value&(Key_Old^Key_Value);
	Key_Old = Key_Value;
	
	switch(Key_Down)
	{
		case Left_Key:   Left_Key_Value = 1;  break;
		case Right_Key:  Right_Key_Value = 1; break;
		case Middel_Key: Middel_Key_Value = 1; break;
	}
	
	switch(Key_UP)
	{
		case Left_Key:   Left_Key_Value = 0;  break;
		case Right_Key:  Right_Key_Value = 0; break;
		case Middel_Key: Middel_Key_Value = 0; break;
	}
	
	if(Key_Down == DPI_Key)
	{
		DPI += 500;
		if(DPI > 3000) DPI = 500;
		DPI_Config(DPI);
        led_flag = 1;
        Key_cnt++;
	}
}

// DPI 指示灯闪烁任务
void LED_Task(void)
{
    static uint32_t LED_tick_ms;
    static uint8_t blink_cnt;
    
    if(led_flag == 1)
    {
        if(SYS_tick_ms - LED_tick_ms >= 500)
        {
            LED_tick_ms = SYS_tick_ms;
            HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
            blink_cnt++;
            if(blink_cnt == 6)
            {
                Key_cnt--;
                blink_cnt = 0;
                HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);
            }
            if(Key_cnt == 0) led_flag = 0;
        }
    }   
}

// 鼠标坐标更新
void Mouse_XY_Updata(void)
{
	Motion_Burst(motion_burst_data);   
	X_Speed = (int16_t)(motion_burst_data[2] + (motion_burst_data[3] << 8));
	Y_Speed = (int16_t)(motion_burst_data[4] + (motion_burst_data[5] << 8));
	myMouse_update(&mouseHID);
}

// 滚轮更新
void Mouse_wheel_Updata(void)
{
	if((int16_t)__HAL_TIM_GET_COUNTER(&htim2) > 0)
		wheel_num = 0xFF;   
	else if((int16_t)__HAL_TIM_GET_COUNTER(&htim2) < 0)
		wheel_num = 0x01;
	else
		wheel_num = 0x80;
	
	TIM2->CNT=0;
}

uint8_t Key_Read(void)
{
	if(HAL_GPIO_ReadPin(GPIOB, Left_Key_Pin) == 0) return Left_Key;
	if(HAL_GPIO_ReadPin(GPIOB, Right_Key_Pin) == 0) return Right_Key;
	if(HAL_GPIO_ReadPin(GPIOB, Middel_Key_Pin) == 0) return Middel_Key;
	if(HAL_GPIO_ReadPin(GPIOB, DPI_Key_Pin) == 0) return DPI_Key;
	return 0;
}	

/* USER CODE END 0 */

int main(void)
{
  /* MCU Configuration--------------------------------------------------------*/
  HAL_Init();
  SystemClock_Config();

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USB_DEVICE_Init();
  MX_SPI1_Init();
  MX_TIM3_Init();
  MX_TIM2_Init();
  MX_I2C2_Init();
  MX_TIM4_Init(); // 初始化 TIM4

  /* USER CODE BEGIN 2 */
  delay_init(72); 
  __HAL_SPI_ENABLE(&hspi1);
  HAL_Delay(100);

  Power_up_sequence();
  myMouse_init(&mouseHID);
  DPI_Config(DPI);
	
  __HAL_TIM_CLEAR_IT(&htim3,TIM_IT_UPDATE ); 
  HAL_TIM_Base_Start_IT(&htim3);
  
  HAL_TIM_Encoder_Start(&htim2, TIM_CHANNEL_ALL);
  
  Task_Init();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
		Task_Run();
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

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
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USB;
  PeriphClkInit.UsbClockSelection = RCC_USBCLKSOURCE_PLL_DIV1_5;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  */
void Error_Handler(void)
{
  __disable_irq();
  while (1)
  {
  }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
