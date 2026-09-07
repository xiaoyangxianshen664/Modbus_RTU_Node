#include "main.h"

void bsp_Init(void){

	HAL_Init();
	/* Configure the system clock to 72 MHz */
    SystemClock_Config();   
    /* 初始化 printf 使用的 USART1。 */
    MyPrintfInit();

    GPIO_Init();
    LED_Init();
    KEY_Init();

}
