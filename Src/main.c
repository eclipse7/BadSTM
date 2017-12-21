/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "stm32f1xx_hal.h"
#include "fatfs.h"
#include "usb_keyboard.h"

/* Private variables ---------------------------------------------------------*/
	SPI_HandleTypeDef hspi1;
	TIM_HandleTypeDef htim2;

	FATFS fs;
	FIL testFile;
	uint8_t readBuffer[16];
	uint8_t path[13] = "script.txt";
	uint8_t readBytes;
	FRESULT res;


#define LINE_LENGTH 512

	uint8_t line_str[512];
	uint8_t line_pointer = 0;
	uint8_t flag_ovf = 0;
	uint8_t byte_ovf = 0;

	uint8_t str_1[512];
	uint8_t str_2[512];

	int number;


/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
void Error_Handler(void);
static void MX_GPIO_Init(void);
static void MX_TIM2_Init(void);



void stringCopyFromTo(char *s_from, char *s_to){
	while(*s_from) {
		*s_to = *s_from;
		s_from++;
		s_to++;
	}
	*s_to = 0;
}

char stringToUInt(char *s, int *n) {	//ok
	int i = 1;
	int length;
	int j;
	char r = 0;

	if (*s != 0){
		*n = 0;
		j = s;
		length = stringLength(s);
		s += (length - 1);
		r = 1;
		while((j <= s) && (r == 1)) {
			if ((*s < 0x30) || (*s > 0x39)) r = 0;
			else {
				*n += (*s - 0x30) * i;
				i *= 10;
				s--;
			}
		}
	}
	return r;
}

int stringLength(char *string) {	//ok
	int i;
	for (i = 0; *string; string++) {
		i++;
	}
	return i;
}

int stringIndexOf(char *string, char symbol) {	//ok
	int i = 0;
	int r = -1;
	if (symbol != 0) {
		while((*string) && (r == -1)) {
			if (*string == symbol) r = i;
			string++;
			i++;
		}
	}
	return r;
}

int stringEquals(char *string1, char *string2) {	//ok
	int i = 0;
	int j,k;
	int r = 0;

	j = stringLength(string1);
	k = stringLength(string2);
	if (j == k) {
		r = 1;
		for (i = 0; i < j; i++) {
			if (*string1 != *string2) r = 0;
			string1++;
			string2++;
		}
	}
	return r;
}

void divideStringByFirstSpace(char *string, char *string1, char *string2){	//ok
	int indexSpace;
	int lengthStr;
	int i;
	indexSpace = stringIndexOf(string, ' ');
	lengthStr = stringLength(string);

	if (indexSpace != -1) {
		for(i = 0; i < indexSpace; i++) {
			*string1 = *string;
			string1++;
			string++;
		}
		*string1 = 0;

		string++;
		for(i = indexSpace + 1; i < lengthStr; i++) {
			*string2 = *string;
			string2++;
			string++;
		}
		*string2 = 0;
	}else{
		for(i = 0; i < lengthStr; i++) {
			*string1 = *string;
			string1++;
			string++;
		}
		*string1 = 0;
		*string2 = 0;
	}
}


void line(char *str) {
	int space1;
	int latest_space;
	int delayTime;

	space1 = stringIndexOf(str, ' ');
	divideStringByFirstSpace(str, &str_1, &str_2);

	if (space1 == -1) {
		press(&str_1);
	}
	else {
		//divideStringByFirstSpace(str, &str_1, &str_2);
		if (stringEquals(&str_1, "STRING")) {
			KeyboardPrint(&str_2);
		}
		else if (stringEquals(&str_1, "DELAY")) {
			if (stringToUInt(&str_2, &delayTime)) {
				HAL_Delay(delayTime);
			}
		}
		else if (stringEquals(&str_1, "REM")) {

		}
		else{
			while(stringLength(str) > 0) {
				latest_space = stringIndexOf(str, ' ');
				if (latest_space == -1){
					press(str);
					str[0] = 0;
				}
				else {
					divideStringByFirstSpace(str, &str_1, &str_2);
					press(&str_1);
					stringCopyFromTo(&str_2, str);
				}
			}
		}
	}

	KeyboardReleaseAll();
}


void press(char *b) {
	char c;

	if (stringLength(b) == 1) {
		c = b[0];
		KeyboardPress(c);
	}
	else if (stringEquals(b, "KEY_PRT_SCR")) {
		KeyboardPress(KEY_PRT_SCR);
	}
	else if (stringEquals(b, "ENTER")) {
		KeyboardPress(KEY_RETURN);
	}
	else if (stringEquals(b, "CTRL")) {
		KeyboardPress(KEY_LEFT_CTRL);
	}
	else if (stringEquals(b, "SHIFT")) {
		KeyboardPress(KEY_LEFT_SHIFT);
	}
	else if (stringEquals(b, "ALT")) {
		KeyboardPress(KEY_LEFT_ALT);
	}
	else if (stringEquals(b, "GUI")) {
		KeyboardPress(KEY_LEFT_GUI);
	}

	else if (stringEquals(b, "RIGHTCTRL")) {
		KeyboardPress(KEY_RIGHT_CTRL);
	}
	else if (stringEquals(b, "RIGHTSHIFT")) {
		KeyboardPress(KEY_RIGHT_SHIFT);
	}
	else if (stringEquals(b, "RIGHTALT")) {
		KeyboardPress(KEY_RIGHT_ALT);
	}
	else if (stringEquals(b, "RIGHTGUI")) {
		KeyboardPress(KEY_RIGHT_GUI);
	}

	else if (stringEquals(b, "UP") || stringEquals(b, "UPARROW")) {
		KeyboardPress(KEY_UP_ARROW);
	}
	else if (stringEquals(b, "DOWN") || stringEquals(b, "DOWNARROW")) {
		KeyboardPress(KEY_DOWN_ARROW);
	}
	else if (stringEquals(b, "LEFT") || stringEquals(b, "LEFT")) {
		KeyboardPress(KEY_LEFT_ARROW);
	}
	else if (stringEquals(b, "RIGHT") || stringEquals(b, "RIGHTARROW")) {
		KeyboardPress(KEY_RIGHT_ARROW);
	}
	else if (stringEquals(b, "BACKSPACE")) {
		KeyboardPress(KEY_BACKSPACE);
	}
	else if (stringEquals(b, "DELETE")) {
		KeyboardPress(KEY_DELETE);
	}
	else if (stringEquals(b, "PAGEUP")) {
		KeyboardPress(KEY_PAGE_UP);
	}
	else if (stringEquals(b, "PAGEDOWN")) {
		KeyboardPress(KEY_PAGE_DOWN);
	}
	else if (stringEquals(b, "HOME")) {
		KeyboardPress(KEY_HOME);
	}
	else if (stringEquals(b, "ESC")) {
		KeyboardPress(KEY_ESC);
	}
	else if (stringEquals(b, "INSERT")) {
		KeyboardPress(KEY_INSERT);
	}
	else if (stringEquals(b, "TAB")) {
		KeyboardPress(KEY_TAB);
	}
	else if (stringEquals(b, "END")) {
		KeyboardPress(KEY_END);
	}
	else if (stringEquals(b, "CAPSLOCK")) {
		KeyboardPress(KEY_CAPS_LOCK);
	}
	else if (stringEquals(b, "F1")) {
		KeyboardPress(KEY_F1);
	}
	else if (stringEquals(b, "F2")) {
		KeyboardPress(KEY_F2);
	}
	else if (stringEquals(b, "F3")) {
		KeyboardPress(KEY_F3);
	}
	else if (stringEquals(b, "F4")) {
		KeyboardPress(KEY_F4);
	}
	else if (stringEquals(b, "F5")) {
		KeyboardPress(KEY_F5);
	}
	else if (stringEquals(b, "F6")) {
		KeyboardPress(KEY_F6);
	}
	else if (stringEquals(b, "F7")) {
		KeyboardPress(KEY_F7);
	}
	else if (stringEquals(b, "F8")) {
		KeyboardPress(KEY_F8);
	}
	else if (stringEquals(b, "F9")) {
		KeyboardPress(KEY_F9);
	}
	else if (stringEquals(b, "F10")) {
		KeyboardPress(KEY_F10);
	}
	else if (stringEquals(b, "F11")) {
		KeyboardPress(KEY_F11);
	}
	else if (stringEquals(b, "F12")) {
		KeyboardPress(KEY_F12);
	}
	else if (stringEquals(b, "SPACE")) {
		KeyboardPress(' ');
	}
}


int main(void) {

	/* Reset of all peripherals, Initializes the Flash interface and the Systick. */
	HAL_Init();

	/* Configure the system clock */
	SystemClock_Config();

	/* Initialize all configured peripherals */
	MX_GPIO_Init();
	MX_USB_DEVICE_Init();

	//MX_TIM2_Init();
	// start timer 2
	//HAL_TIM_Base_Start_IT(&htim2);

	// Keyboard test
	HAL_Delay(2000);
	KeyboardReleaseAll();

	//KeyboardPrint("Test1\n");

	if (disk_initialize(0) == 0) {
		HAL_Delay(100);
		if(f_mount(&fs, "", 0) == FR_OK) {

			path[12] = '\0';
			if(f_open(&testFile, (char*)path, FA_READ | FA_OPEN_EXISTING) == FR_OK) {

				line_str[0] = 0x00;
				line_pointer = 0;
				flag_ovf = 0;
				while(!f_eof(&testFile)){
					if (flag_ovf == 0) f_read(&testFile, readBuffer, 1, &readBytes);
					else {
						flag_ovf = 0;
						readBuffer[0] = byte_ovf;
					}

					if ((readBuffer[0] == '\n') || (line_pointer == (LINE_LENGTH - 1))) {
						if (line_pointer == (LINE_LENGTH - 1)) {
							flag_ovf = 1;
							byte_ovf = readBuffer[0];
						}
						line_str[line_pointer] = 0x00;

						line(&line_str);
						//KeyboardPrint(&line_str); // print line
						//KeyboardWrite('\n');

						line_str[0] = 0x00;
						line_pointer = 0;
					} else {
						if (readBuffer[0] != 13) {    // magic but it's work
							line_str[line_pointer] = readBuffer[0];
							line_pointer++;
						}
					}
				}

				res = f_close(&testFile);
				HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
			}
		}
	}

	// Endless Loop
	while (1) {

	}

}

/** System Clock Configuration
*/
void SystemClock_Config(void)
{

  RCC_OscInitTypeDef RCC_OscInitStruct;
  RCC_ClkInitTypeDef RCC_ClkInitStruct;
  RCC_PeriphCLKInitTypeDef PeriphClkInit;

    /**Initializes the CPU, AHB and APB busses clocks 
    */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL6;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

    /**Initializes the CPU, AHB and APB busses clocks 
    */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
  {
    Error_Handler();
  }

  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USB;
  PeriphClkInit.UsbClockSelection = RCC_USBCLKSOURCE_PLL;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }

    /**Configure the Systick interrupt time 
    */
  HAL_SYSTICK_Config(HAL_RCC_GetHCLKFreq()/1000);

    /**Configure the Systick 
    */
  HAL_SYSTICK_CLKSourceConfig(SYSTICK_CLKSOURCE_HCLK);

  /* SysTick_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(SysTick_IRQn, 0, 0);
}

/* TIM2 init function */
static void MX_TIM2_Init(void)
{

  TIM_ClockConfigTypeDef sClockSourceConfig;
  TIM_MasterConfigTypeDef sMasterConfig;

  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 48000;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 10;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  if (HAL_TIM_Base_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }

  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim2, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }

  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }

}

/** Configure pins as 
        * Analog 
        * Input 
        * Output
        * EVENT_OUT
        * EXTI
*/
static void MX_GPIO_Init(void)
{

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();

    GPIO_InitTypeDef GPIO_InitStruct;
    /*Configure GPIO pin : PC13 */
    GPIO_InitStruct.Pin = GPIO_PIN_13;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
    // LED off
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @param  None
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler */
  /* User can add his own implementation to report the HAL error return state */
  while(1) 
  {
  }
  /* USER CODE END Error_Handler */ 
}

#ifdef USE_FULL_ASSERT

/**
   * @brief Reports the name of the source file and the source line number
   * where the assert_param error has occurred.
   * @param file: pointer to the source file name
   * @param line: assert_param error line source number
   * @retval None
   */
void assert_failed(uint8_t* file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
    ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */

}

#endif

/**
  * @}
  */ 

/**
  * @}
*/ 

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
