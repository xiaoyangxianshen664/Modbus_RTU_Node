/**
  ******************************************************************************
  * @file    freertos_demo.c
  * @brief   FreeRTOS 内存管理实验
  * @note    演示 FreeRTOS 堆内存的申请、释放和查询。
  *          KEY1 → pvPortMalloc(30) 申请 30 字节
  *          KEY2 → vPortFree(buf)   释放之前申请的内存
  *          每 500ms → xPortGetFreeHeapSize() 打印堆剩余大小
  *          堆大小由 FreeRTOSConfig.h 中 configTOTAL_HEAP_SIZE 决定（36KB）。
  ******************************************************************************
*/

#include "freertos_demo.h"
#include "FreeRTOS.h"
#include "task.h"
#include "./LED/LED.h"
#include "./Key/Key.h"
#include <stdio.h>

/* ── 空闲任务静态内存 ── */
static StaticTask_t xIdleTaskTCB;
static StackType_t  uxIdleTaskStack[configMINIMAL_STACK_SIZE];

void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer,
                                   StackType_t **ppxIdleTaskStackBuffer,
                                   uint32_t *pulIdleTaskStackSize)
{
    *ppxIdleTaskTCBBuffer   = &xIdleTaskTCB;
    *ppxIdleTaskStackBuffer = uxIdleTaskStack;
    *pulIdleTaskStackSize   = configMINIMAL_STACK_SIZE;
}

/* ── 定时器服务任务静态内存 ── */
static StaticTask_t xTimerTaskTCB;
static StackType_t  uxTimerTaskStack[configTIMER_TASK_STACK_DEPTH];

void vApplicationGetTimerTaskMemory(StaticTask_t **ppxTimerTaskTCBBuffer,
                                    StackType_t **ppxTimerTaskStackBuffer,
                                    uint32_t    *pulTimerTaskStackSize)
{
    *ppxTimerTaskTCBBuffer   = &xTimerTaskTCB;
    *ppxTimerTaskStackBuffer = uxTimerTaskStack;
    *pulTimerTaskStackSize   = configTIMER_TASK_STACK_DEPTH;
}

/* ── 任务宏配置 ── */
#define START_TASK_PRIO       1
#define TASK1_PRIO            2
#define START_TASK_STACK_SIZE 128
#define TASK_STACK_SIZE       128

TaskHandle_t StartTask_Handler;
TaskHandle_t Task1_Handler;

static void start_task(void *pvParameters);
static void task1(void *pvParameters);


void freertos_demo(void)
{
    xTaskCreate((TaskFunction_t)        start_task,
                (char *)                "start_task",
                (uint16_t)              START_TASK_STACK_SIZE,
                (void *)                NULL,
                (UBaseType_t)           START_TASK_PRIO,
                (TaskHandle_t *)        &StartTask_Handler);
    vTaskStartScheduler();
}


static void start_task(void *pvParameters)
{
    xTaskCreate((TaskFunction_t)        task1,
                (char *)                "task1",
                (uint16_t)              TASK_STACK_SIZE,
                (void *)                NULL,
                (UBaseType_t)           TASK1_PRIO,
                (TaskHandle_t *)        &Task1_Handler);

    vTaskDelete(NULL);
}


/**
 * @brief  任务一 — 申请/释放内存，定期展示堆剩余
 */
static void task1(void *pvParameters)
{
    uint8_t  key = 0;
    uint8_t  t   = 0;                   /* 周期性计数器 */
    uint8_t *buf = NULL;                /* 指向申请到的内存 */

    while (1)
    {
        /*
         * 申请内存 — pvPortMalloc
         * 原型：void *pvPortMalloc(size_t xWantedSize);
         * 参数：xWantedSize = 要多少字节
         * 返值：成功返回指针，失败返回 NULL
         * 内存来源：heap_4.c 的 ucHeap[configTOTAL_HEAP_SIZE(36KB)]
         */
        key = Key_Scan(KEY1_GPIO_PORT, KEY1_PIN);
        if (key == KEY_ON)
        {
            buf = pvPortMalloc(30);                 /* 从堆里申请 30 字节 */
            if (buf != NULL)
                printf("申请内存成功！\r\n");
            else
                printf("申请内存失败！\r\n");
        }

        /*
         * 释放内存 — vPortFree
         * 原型：void vPortFree(void *pv);
         * 参数：pv = 之前 pvPortMalloc 返回的指针
         * 传 NULL 会直接 return，安全
         */
        key = Key_Scan(KEY2_GPIO_PORT, KEY2_PIN);
        if (key == KEY_ON)
        {
            if (buf != NULL)
            {
                vPortFree(buf);                     /* 把申请的内存还回去 */
                printf("释放内存成功！\r\n");
                buf = NULL;
            }
        }

        /*
         * 每 500ms 打印一次堆剩余大小
         *
         * 查询堆剩余 — xPortGetFreeHeapSize
         * 原型：size_t xPortGetFreeHeapSize(void);
         * 返值：堆里还剩多少字节空闲
         * 还有 xPortGetMinimumEverFreeHeapSize() 查历史最小值
         */
        if (t++ > 50)
        {
            t = 0;
            printf("堆剩余：%lu 字节\r\n",
                   (unsigned long)xPortGetFreeHeapSize());
        }

        vTaskDelay(10);
    }
}
