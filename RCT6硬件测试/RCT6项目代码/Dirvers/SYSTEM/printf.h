#ifndef __PRINTF_H
#define __PRINTF_H

#include "stm32f1xx_hal.h"

void MyPrintfInit(void);
void myprintf(const char *format, ...);
void myprintf_n(const char *format, ...);

#endif /* __PRINTF_H */
