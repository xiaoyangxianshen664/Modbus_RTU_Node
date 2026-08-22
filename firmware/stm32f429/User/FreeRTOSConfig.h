#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include "stm32f4xx.h"
#include "./Usart/Usart.h"

/* 针对不同编译器，包含不同的 stdint.h 文件 */
#if defined(__ICCARM__) || defined(__CC_ARM) || defined(__GNUC__)
#include <stdint.h>
extern uint32_t SystemCoreClock;
#endif

/* -----------------------------------------------------------------------------
 *                              断言配置
 * -----------------------------------------------------------------------------
 */
/* 断言：失败时关中断死循环，不用 printf 避免头文件依赖 */
#define configASSERT(x)           \
    if ((x) == 0)                 \
    {                             \
        taskDISABLE_INTERRUPTS(); \
        for (;;)                  \
            ;                     \
    }

/* -----------------------------------------------------------------------------
 *                          FreeRTOS 基础配置选项
 * -----------------------------------------------------------------------------
 */
/* 1 = 抢占式调度，0 = 协作式调度（没有时间片，任务主动释放 CPU） */
#define configUSE_PREEMPTION 1                    // 开启抢占式调度

/* 1 = 同优先级任务时间片轮转 */
#define configUSE_TIME_SLICING 1                 // 开启同优先级的任务时间片调度

/* 1 = 使用硬件前导零指令 [CLZ] 优化任务选择，M4 支持 */
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 1 // 帮助调度器找到最高优先级的就绪任务

/* 1 = 开启低功耗 tickless 模式（空闲时停 SysTick 省电）
 * 注意：tickless 可能导致下载失败，因为 MCU 进入睡眠后调试器连不上
 * 恢复方法：
 *    1. BOOT0 接高电平(3.3V) → 上电 → 擦除芯片 → 重新下载
 *    2. 或用 FlyMcu 擦除芯片：STMISP → 擦除芯片
 * 野火注释里也提醒了，开了以后下载失败不好恢复，初学阶段建议保持 0。
 */
#define configUSE_TICKLESS_IDLE 1                /* ★ 开 Tickless：Idle 不空转，WFI 睡觉 */

/* CPU 内核时钟频率，即 HCLK = 180MHz（F429 主频） */
#define configCPU_CLOCK_HZ (SystemCoreClock)

/* RTOS 心跳频率 Hz，1000 = 每 1ms 一次 SysTick 中断 */
#define configTICK_RATE_HZ ((TickType_t)1000)

/* 最大优先级数，0~31 共 32 级（数字越大优先级越高） */
#define configMAX_PRIORITIES (32)

/* 空闲任务的最小栈大小，单位 word（4 字节），即 512 字节 */
#define configMINIMAL_STACK_SIZE ((unsigned short)128)

/* 任务名最大长度（含 '\0'）
 * "LedTask" 占 8 字节，可写到 15 字符；设大了浪费栈，设小了名字截断。
 */
#define configMAX_TASK_NAME_LEN (16)

/* 1 = 使用 16 位 TickType_t，0 = 32 位
 * 32 位可避免长时间运行后 Tick 溢出回绕引发的问题。
 * F429 资源充足，选用 32 位；16 位只用在内存极度紧张的单片机（几 KB RAM 的 M0）。
 */
#define configUSE_16_BIT_TICKS 0

/* 1 = 空闲任务主动让出 CPU 给同优先级任务 */
#define configIDLE_SHOULD_YIELD 1

/* 开启队列集
 * 用于一个任务同时监听多个队列（类似 select()），暂未使用，关闭以省代码空间。
 */
#define configUSE_QUEUE_SETS 1               /* 开启队列集 */

/* 开启任务通知功能
 * 轻量级信号量，不占堆内存，比信号量快；每个任务只有一个通知通道。
 */
#define configUSE_TASK_NOTIFICATIONS 1

/* -----------------------------------------------------------------------------
 *                          信号量与互斥量配置
 * -----------------------------------------------------------------------------
 */
/* 开启互斥信号量
 * 保护共享资源（I2C 总线、SPI Flash），同一时刻只有一个任务持有。
 * 示例：
 *     xSemaphoreTake(i2c_mutex, portMAX_DELAY);   // 上锁，别人等着
 *     操作 I2C EEPROM...
 *     xSemaphoreGive(i2c_mutex);                  // 解锁
 * 与普通信号量的区别：互斥锁有优先级继承，防止优先级反转。
 * 开 0 是还没学到，后面用的时候再开。
 */
#define configUSE_MUTEXES 1                 /* 开启互斥锁 */

/* 开启递归互斥信号量
 * 同一任务可连续 lock 多次不卡死自己（计数），普通互斥锁 lock 两次直接死锁。
 * 几乎用不到，只有在任务自己调自己（函数递归）且中间要拿锁时才需要。
 */
#define configUSE_RECURSIVE_MUTEXES 0

/* 开启计数信号量
 * 管理有限资源（如 3 个 DMA buffer，初始值=3，用完归还）。
 * 跟二进制信号量的区别：计数型表示"还有几个空闲位"。
 * 后面用到了再开。
 */
#define configUSE_COUNTING_SEMAPHORES 1                 /* 开启计数信号量 */

/* 注册的队列和信号量数量上限（调试用）
 * 调试时可通过队列注册查看名称及排队数据，开发阶段开着 10 就行。
 */
#define configQUEUE_REGISTRY_SIZE 10

/* 给任务打标签（void* 指针），高级调试功能，平时用不到 */
#define configUSE_APPLICATION_TASK_TAG 0

/* -----------------------------------------------------------------------------
 *                          FreeRTOS 内存管理配置
 * -----------------------------------------------------------------------------
 */
/* 支持动态内存分配（xTaskCreate 需要）
 * xTaskCreate 内部自动从堆里 malloc 任务栈和 TCB，不用手动管内存。
 */
#define configSUPPORT_DYNAMIC_ALLOCATION 1

/* 支持静态内存分配 */
#define configSUPPORT_STATIC_ALLOCATION 1

/* FreeRTOS 堆总大小，36KB
 * F429 芯片 SRAM = 256KB，此堆为 FreeRTOS 自留地：
 *   ├─ 任务栈（如 LedTask 128 * 4=512B）
 *   ├─ 空闲任务栈
 *   └─ 队列/信号量等均从此分配
 * 不够时 xTaskCreate 返回 NULL，调大此值即可。
 */
#define configTOTAL_HEAP_SIZE ((size_t)(36 * 1024))

/* -----------------------------------------------------------------------------
 *                          FreeRTOS 钩子函数
 * -----------------------------------------------------------------------------
 */
/* 1 = 使能空闲钩子 vApplicationIdleHook()
 * 可用于释放被删除任务的栈内存或进入低功耗；不能阻塞。目前用不到，关闭。
 */
#define configUSE_IDLE_HOOK 0

/* 1 = 使能 Tick 钩子 vApplicationTickHook()
 * 在每个 SysTick 中断末尾调用，必须极短。几乎用不到，关闭。
 */
#define configUSE_TICK_HOOK 0

/* 使能内存申请失败钩子
 * 堆用完时的报警机制；开发阶段可开，但调大堆比接钩子更有用，此处关。
 */
#define configUSE_MALLOC_FAILED_HOOK 0

/* 栈溢出检测：0=关，1=方法一，2=方法二
 * 开发阶段必开（防随机覆盖），此处暂关，建议后期开 2。
 */
#define configCHECK_FOR_STACK_OVERFLOW 0

/* -----------------------------------------------------------------------------
 *                       FreeRTOS 运行状态与调试
 * -----------------------------------------------------------------------------
 */
/* 开启运行时统计（需配合定时器提供时基）
 * 可统计每个任务占用 CPU 时间。下面两个 port 宏负责桥接硬件定时器。
 */
#define configGENERATE_RUN_TIME_STATS 1						//用于控制是否开启“任务运行时间统计”功能的配置宏。
#include "../BSP/TIM6/TIM6.h"                                               /* ConfigureTimeForRunTimeStats 声明 */
#define portCONFIGURE_TIMER_FOR_RUN_TIME_STATS()  ConfigureTimeForRunTimeStats()
#define portGET_RUN_TIME_COUNTER_VALUE()          FreeRTOSRunTimeTicks        /* TIM6 全局计数器 */

/* 开启可视化追踪（配合 Tracealyzer）
 * uxTaskGetSystemState / vTaskGetInfo / vTaskList 等 API 需要此宏。
 */
#define configUSE_TRACE_FACILITY 1

/* 配合 configUSE_TRACE_FACILITY=1，开启 vTaskList() 等格式化函数
 * 可将任务状态、栈剩余等格式化成字符串打印，开发阶段建议开。
 */
#define configUSE_STATS_FORMATTING_FUNCTIONS 1

/* -----------------------------------------------------------------------------
 *                          FreeRTOS 协程（一般不用）
 * -----------------------------------------------------------------------------
 */
#define configUSE_CO_ROUTINES 0                 // 保留兼容，关着即可
#define configMAX_CO_ROUTINE_PRIORITIES (2)     // 无意义占位

/* -----------------------------------------------------------------------------
 *                          FreeRTOS 软件定时器
 * -----------------------------------------------------------------------------
 */
#define configUSE_TIMERS 1                      // ★ 使能软件定时器
#define configTIMER_TASK_PRIORITY (configMAX_PRIORITIES - 1) // 最高优先级
#define configTIMER_QUEUE_LENGTH 10             // 命令队列长度
#define configTIMER_TASK_STACK_DEPTH (configMINIMAL_STACK_SIZE * 2) // 256 word

/* -----------------------------------------------------------------------------
 *                       FreeRTOS 可选 API 函数宏开启标示
 * -----------------------------------------------------------------------------
 */
#define INCLUDE_xTaskGetSchedulerState 1    // 查调度器状态
#define INCLUDE_vTaskPrioritySet 1          // 运行时改任务优先级
#define INCLUDE_uxTaskPriorityGet 1         // 查任务优先级
#define INCLUDE_vTaskDelete 1               // 删除任务
#define INCLUDE_vTaskCleanUpResources 1     // 配合删除回收内存
#define INCLUDE_vTaskSuspend 1              // 挂起/解挂任务
#define INCLUDE_xTaskResumeFromISR 1        // ISR 里恢复挂起任务
#define INCLUDE_vTaskDelayUntil 1           // 绝对延时（周期任务）
#define INCLUDE_vTaskDelay 1                // 相对延时（最常用）
#define INCLUDE_eTaskGetState 1             // 查任务状态
#define INCLUDE_xTaskGetHandle 1            // 根据任务名反查句柄
#define INCLUDE_uxTaskGetStackHighWaterMark 1 // 查栈历史最小剩余
#define INCLUDE_xTaskGetCurrentTaskHandle 1 // 获取当前任务句柄
#define INCLUDE_xTimerPendFunctionCall 0    // 软定时器关，故不需要

/* -----------------------------------------------------------------------------
 *                          FreeRTOS 中断配置
 * -----------------------------------------------------------------------------
 */
/* ARM Cortex-M 的 NVIC 优先级位数（F429 固定 4 位，0~15 共 16 级） */
#ifdef __NVIC_PRIO_BITS
#define configPRIO_BITS __NVIC_PRIO_BITS
#else
#define configPRIO_BITS 4
#endif

/* 最低中断优先级（15 最低，0 最高） */
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY 15

/* 系统可管理的中断最大优先级
 * 0~4 的硬件中断 NVIC 可以打断任务，但不可调用 RTOS API；
 * 5~15 的 ISR 可调用 API。
 */
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5

/* 内核中断优先级（PendSV 和 SysTick 使用）
 * 计算：15 << (8 - 4) = 15 << 4 = 0xF0
 * 将 PendSV/SysTick 设为最低优先级，确保切换不抢中断。
 */
#define configKERNEL_INTERRUPT_PRIORITY \
    (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))

/* 临界区屏蔽阈值（BASEPRI 值）
 * 硬编码为 0x50 (5 << 4)，绕过 ARMCC V5 内联汇编常量展开 bug。
 * 含义：BASEPRI = 0x50 时，屏蔽所有优先级数值 >= 0x50（等级 5~15）的中断。
 * taskEXIT_CRITICAL() 把 BASEPRI 归零，憋着的 PendSV 立刻触发切任务。
 */
#define configMAX_SYSCALL_INTERRUPT_PRIORITY 0x50

/* -----------------------------------------------------------------------------
 *                       FreeRTOS 中断服务函数映射
 * -----------------------------------------------------------------------------
 */
#define xPortPendSVHandler PendSV_Handler
#define vPortSVCHandler SVC_Handler

/* -----------------------------------------------------------------------------
 *                       Tracealyzer 追踪（默认关）
 * -----------------------------------------------------------------------------
 */
#if (configUSE_TRACE_FACILITY == 1)
//#include "trcRecorder.h"               // Tracealyzer 记录库（我们不用）
// INCLUDE_xTaskGetCurrentTaskHandle 已在上方定义
#endif

#endif /* FREERTOS_CONFIG_H */
