/**
  ******************************************************************************
  * @file    freertos_demo.c
  * @brief   阶段 4 FreeRTOS Modbus + ADC DMA 采集应用
  * @note    ModbusTask 处理通信，MonitorTask 监控周期，
  *          AcquireTask 处理 ADC DMA 数据，LogTask 模拟低优先级日志负载。
  ******************************************************************************
  */

#include "freertos_demo.h"
#include "FreeRTOS.h"
#include "task.h"													//"task.h"使用FreeRTOS任务及任务通知
#include "semphr.h"												//"semphr.h"使用FreeRTOS信号量（互斥锁）
#include "queue.h"												//`queue.h`：使用 FreeRTOS 队列；
#include "./Fatfs/ff.h"										//`ff.h`：调用 FatFs 文件 API；
#include "./Fatfs/ff_gen_drv.h"						//`ff_gen_drv.h`：注册 SD 驱动；
#include "./Fatfs/sd_diskio.h"						//- `sd_diskio.h`：获取 `SD_Driver`；
#include "./SDIO/bsp_sdio_sd.h"						//`bsp_sdio_sd.h`：访问 SD HAL 句柄和错误信息。
#include "./IWDG/iwdg.h"										// 独立看门狗配置和喂狗接口
#include "./Config/config_storage.h"										// W25Q256 配置持久化接口
#include "./485/bsp_485.h"								//RS485传输数据
#include "./RTC/rtc.h"									  //`rtc.h`：读取 RTC 时间；
#include "./ADC/ADC_Multi.h"							//ADC采集数据
#include <string.h>
#include <math.h>
#include <stdio.h>
/*算法*/
#include "modbus_transport.h"
#include "modbus_rtu.h"
#include "modbus_registers.h"

/* ════════════════════════════════════════════════════════════
 * 阶段 3 任务优先级配置
 *
 * FreeRTOS 中，数字越大表示任务优先级越高。
 * 高优先级任务抢占低优先级任务，但任务本身仍然需要主动阻塞或延时，
 * 否则可能长期占用 CPU。
 * ════════════════════════════════════════════════════════════ */

#define MONITOR_TASK_PRIORITY     4U  // MonitorTask：最高业务优先级，用于及时执行系统健康监测
#define MODBUS_TASK_PRIORITY      3U  // ModbusTask：收到完整 Modbus 帧后及时解析并发送响应
#define ACQUIRE_TASK_PRIORITY     2U  // AcquireTask：周期采集数据并更新输入寄存器
#define LOG_TASK_PRIORITY         1U  // LogTask：最低业务优先级，执行日志或模拟耗时操作

/* ════════════════════════════════════════════════════════════
 * 阶段 3 任务栈大小配置
 *
 * xTaskCreate() 的栈大小单位不是字节，而是 StackType_t 的数量。
 * 在 STM32F429 的 Cortex-M4 工程中，StackType_t 通常为 32 位，
 * 因此 1 个 StackType_t = 4 字节。
 *
 * 例如：
 *   512U = 512 × 4 = 2048 字节
 *   128U = 128 × 4 =  512 字节
 *
 * ModbusTask 需要调用协议层、使用局部数组和执行 memcpy，
 * 所以分配更大的栈空间。
 * ════════════════════════════════════════════════════════════ */

#define MODBUS_TASK_STACK_SIZE    512U  // ModbusTask 栈：512 word，约 2KB
#define MONITOR_TASK_STACK_SIZE   128U  // MonitorTask 栈：128 word，约 512B
#define ACQUIRE_TASK_STACK_SIZE   512U  // AcquireTask 栈：512 word，约 2KB（含 logf 浮点换算调用链）
#define LOG_TASK_STACK_SIZE       512U  // LogTask 栈：512 word，约 2KB（FatFs/f_printf 调用链较深）
#define LOG_QUEUE_LENGTH          16U   // LogQueue 最多同时保存 16 个 log_record_t 结构体，也就是 16 条完整采样记录。

/* ════════════════════════════════════════════════════════════
 * 周期任务的运行周期
 *
 * ModbusTask 不设置固定周期，而是一直阻塞等待完整帧通知。
 * 其他三个任务按照各自的周期运行。
 * ════════════════════════════════════════════════════════════ */

#define MONITOR_PERIOD_MS         100U   // MonitorTask 每 100ms 执行一次健康监测
#define ACQUIRE_PERIOD_MS         100U   // 无 DMA 通知时的初始等待时间
#define LOG_PERIOD_MS             1000U  // LogTask 每 1s 获取一次寄存器快照

/* ════════════════════════════════════════════════════════════
 * 模拟日志负载配置
 *
 * 阶段 3 暂时没有接入 SD 卡，这里用空循环模拟日志写入的耗时，
 * 用来验证低优先级任务运行期间，Modbus 通信是否仍能稳定响应。
 *
 * 阶段 5 接入 RTC 和 SD 卡后，这个模拟循环会替换成真正的日志写入。
 * ════════════════════════════════════════════════════════════ */

#define LOG_SIMULATED_LOAD_COUNT  200000U  // 模拟一次耗时日志操作的循环次数

/* NTC 分压和 Beta 模型参数：当前模块为 R25=10KΩ、固定电阻=10KΩ、B=3950。 */
#define NTC_R25_OHM                 10000.0f  // NTC 在 25℃ 时的标称电阻
#define NTC_FIXED_RESISTOR_OHM      10000.0f  // ADC 分压电路中的固定电阻
#define NTC_BETA                    3950.0f  // NTC 的 Beta 参数
#define NTC_T25_K                   298.15f  // 25℃ 换算成开尔文：25+273.15

/* ════════════════════════════════════════════════════════════
 * FreeRTOS 任务句柄
 *
 * 任务句柄是 FreeRTOS 为每个任务创建的身份标识。
 * 后续可以通过任务句柄向指定任务发送通知、查询任务状态或删除任务。
 *
 * 例如：
 *   ModbusTaskHandle
 *       └── T3.5 定时器中断通过它唤醒 ModbusTask
 * ════════════════════════════════════════════════════════════ */

static TaskHandle_t ModbusTaskHandle;   // ModbusTask 的句柄，用于接收 T3.5 中断通知
static TaskHandle_t MonitorTaskHandle;  // MonitorTask 的句柄，后续可用于监控或查询任务状态
static TaskHandle_t AcquireTaskHandle;  // AcquireTask 的句柄，后续可用于通知采集任务
static TaskHandle_t LogTaskHandle;      // LogTask 的句柄，后续可用于通知日志任务

/* ════════════════════════════════════════════════════════════
 * 寄存器互斥锁
 *
 * g_registers 会被多个任务访问：
 *
 *   ModbusTask   读取或修改寄存器
 *   AcquireTask  更新 input[] 输入寄存器
 *   LogTask      复制寄存器快照
 *
 * 同一时刻只能允许一个任务访问共享寄存器，因此使用互斥锁保护。
 * 访问前调用 xSemaphoreTake()，访问完成后调用 xSemaphoreGive()。
 * ════════════════════════════════════════════════════════════ */

static SemaphoreHandle_t RegisterMutex;  // 保护 g_registers，避免多个任务同时读写寄存器
static QueueHandle_t LogQueue;            // AcquireTask 到 LogTask 的采集快照队列

typedef struct
{
    RTC_TimeTypeDef time;                // 采集时刻的时分秒
    RTC_DateTypeDef date;                // 采集时刻的年月日
    uint16_t pot_raw;                    // 电位器 ADC 原始值
    uint16_t pot_x10;                    // 电位器工程量 ×10
    uint16_t ntc_raw;                    // NTC ADC 原始值
    uint16_t ntc_temp_x10;               // 温度 ×10
    uint16_t alarm_status;               // 报警状态
} log_record_t;

static FATFS g_fatfs;                    // FatFs 文件系统对象
static char g_fatfs_path[4];             // 逻辑盘路径，例如 "0:/"
static volatile uint32_t g_log_drop_count; // 队列满而丢弃的日志条数
static volatile uint8_t g_log_storage_ready; // 1=FatFs 已挂载，0=SD 不可用
static volatile uint32_t g_reset_cause;         // 保存上一次复位原因
static volatile uint32_t g_health_fault_count;  // MonitorTask 检测到系统不健康时累计的故障次数
static volatile uint32_t g_log_write_error_count; // SD 文件打开或写入失败的次数
static uint32_t g_last_acquire_heartbeat;       // 上一轮 MonitorTask 记录的采集心跳
static uint32_t g_last_log_heartbeat;           // 上一轮 MonitorTask 记录的日志心跳
static uint8_t g_health_snapshot_valid;         // 是否已经建立心跳基线：0 表示第一次检查、还没有基线；1 表示已经可以正式比较心跳。
static uint8_t g_acquire_stall_ticks;            // 每100ms+1，达到 20 次约 2 秒才判定采集任务异常。
static uint8_t g_log_stall_ticks;               // 每100ms+1，达到 20 次约 2 秒才判定日志任务停滞。

/* ════════════════════════════════════════════════════════════
 * 共享 Modbus 寄存器表
 *
 * 这是整个应用层共享的实时寄存器数据。
 *
 *   holding[]：保持寄存器，可被 Modbus 写功能码修改
 *   input[]  ：输入寄存器，由采集任务更新，通常只读
 *
 * 访问 g_registers 前必须先获取 RegisterMutex。
 * ════════════════════════════════════════════════════════════ */

static modbus_registers_t g_registers; 								 // 各任务共享的 Modbus 寄存器映射表

/* ════════════════════════════════════════════════════════════
 * Modbus 请求和响应缓冲区
 *
 * g_request：
 *   保存从 USART2 中断接收缓冲区复制过来的完整请求帧。
 *   复制完成后交给纯 C 协议层解析。
 *
 * g_response：
 *   保存协议层构造出的完整响应帧。
 *   包括从站地址、功能码、数据和 CRC，最后交给 RS485 发送函数。
 *
 * 两个数组都按照 Modbus RTU 最大 ADU 长度分配。
 * 当前 MODBUS_MAX_ADU_SIZE 为 256 字节。
 * ════════════════════════════════════════════════════════════ */

static uint8_t g_request[MODBUS_MAX_ADU_SIZE];   // ModbusTask 的请求帧副本，避免长期占用 ISR 接收缓冲区
static uint8_t g_response[MODBUS_MAX_ADU_SIZE];  // 协议层生成的响应帧，准备通过 RS485 发送

/* ════════════════════════════════════════════════════════════
 * 任务运行心跳和调试状态
 *
 * 这些变量用于观察各任务是否按照预期运行。
 * 当前阶段主要用于调试和验证，后续阶段可以用于：
 *
 *   - MonitorTask 检查其他任务是否卡死
 *   - 看门狗喂狗条件判断
 *   - 故障诊断和运行状态统计
 * ════════════════════════════════════════════════════════════ */

static volatile uint32_t g_modbus_heartbeat;   // ModbusTask 每成功处理一帧后加 1
static volatile uint32_t g_monitor_heartbeat;  // MonitorTask 每 100ms 执行一次后加 1
static volatile uint32_t g_acquire_heartbeat;  // AcquireTask 每完成一批 ADC 采样后加 1
static volatile uint32_t g_log_heartbeat;      // LogTask 每完成一次模拟日志操作后加 1
static volatile uint16_t g_last_logged_input;  // LogTask 最近一次复制并记录的 input[0] 数值

/* ════════════════════════════════════════════════════════════
 * ModbusTask 就绪标志
 *
 * TIM4 的 T3.5 中断可能早于 ModbusTask 完成启动。
 * 如果任务还没有开始运行，中断不能向它发送有效通知。
 *
 * ModbusTask 开始执行后会将该标志置为 1，
 * modbus_task_notify_from_isr() 只有在该标志有效时才发送任务通知。
 * ════════════════════════════════════════════════════════════ */

static volatile uint8_t g_modbus_task_ready;  // 1 表示 ModbusTask 已启动，可以接收 ISR 通知
static volatile uint8_t g_acquire_task_ready; // 1 表示 AcquireTask 已启动，可以接收 DMA 通知

/* ════════════════════════════════════════════════════════════
 * Idle Task 和 Timer Task 的静态内存
 *
 * 当 FreeRTOS 配置为使用静态任务内存时，
 * 系统需要由应用程序提供空闲任务和软件定时器任务的 TCB 与栈空间。
 *
 *   xIdleTaskTCB / uxIdleTaskStack：
 *       提供 Idle Task 使用的控制块和栈。
 *
 *   xTimerTaskTCB / uxTimerTaskStack：
 *       提供 Timer Service Task 使用的控制块和栈。
 *
 * 这两个任务不是我们自己创建的业务任务，
 * 而是 FreeRTOS 内核运行所需的系统任务。
 * ════════════════════════════════════════════════════════════ */

static StaticTask_t xIdleTaskTCB;  // Idle Task 的静态任务控制块
static StackType_t uxIdleTaskStack[configMINIMAL_STACK_SIZE];  // Idle Task 的静态栈空间
static StaticTask_t xTimerTaskTCB;  // Timer Service Task 的静态任务控制块
static StackType_t uxTimerTaskStack[configTIMER_TASK_STACK_DEPTH];  // Timer Task 的静态栈空间



/* ════════════════════════════════════════════════════════════
 * 任务函数的前置声明
 *
 * 这些函数在本文件后面定义。
 * 先声明后使用，可以让 freertos_demo() 在前面调用 xTaskCreate()
 * 时识别这些任务函数。
 *
 * 任务函数一般不会返回，内部通常是 for (;;)
 * 循环，并通过阻塞或延时把 CPU 让给其他任务。
 * ════════════════════════════════════════════════════════════ */

static void modbus_task(void *pvParameters);   // 等待完整帧通知，解析 Modbus 请求并发送响应
static void monitor_task(void *pvParameters);  // 100ms 周期执行系统健康监测
static void acquire_task(void *pvParameters);  // 100ms 周期采集数据并更新 input[] 寄存器
static void log_task(void *pvParameters);      // 从队列取快照并写入 SD 日志文件

/**
 * @brief  读取并保存 MCU 上一次复位原因
 * @param  无
 * @return 无
 * @note   RCC->CSR 的复位标志必须在启动早期读取；随后清除，避免下一次启动混入旧原因。
 *         保存值的 bit0~bit6 依次表示 IWDG、WWDG、软件、外部引脚、上电、BOR、低功耗复位。
 */
void diagnostics_init(void)																 //复位诊断初始化函数
{
    g_reset_cause = 0U;                                    // 先清空应用层使用的紧凑复位原因位图
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST) != RESET) g_reset_cause |= (1U << 0); // 独立看门狗复位
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_WWDGRST) != RESET) g_reset_cause |= (1U << 1); // 窗口看门狗复位
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_SFTRST) != RESET)  g_reset_cause |= (1U << 2); // 软件复位
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_PINRST) != RESET)  g_reset_cause |= (1U << 3); // 外部 NRST 复位
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_PORRST) != RESET)  g_reset_cause |= (1U << 4); // 上电/掉电复位
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_BORRST) != RESET)  g_reset_cause |= (1U << 5); // 欠压复位
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_LPWRRST) != RESET) g_reset_cause |= (1U << 6); // 低功耗复位
    __HAL_RCC_CLEAR_RESET_FLAGS();                        // 清除硬件标志，等待记录下一次真实复位
}

/**
 * @brief  将 NTC ADC 原始值换算为温度（单位：0.1℃）
 * @param  adc_raw ADC 原始值，12 位范围为 0～4095
 * @return 温度放大 10 倍后的非负整数；例如 316 表示 31.6℃
 * @note   使用 R25=10KΩ、固定电阻=10KΩ、B=3950 的 Beta 模型。
 * @example uint16_t temp_x10 = ntc_adc_to_temp_x10(1750U);
 */
static uint16_t ntc_adc_to_temp_x10(uint16_t adc_raw)
{
    float resistance;                                      // 根据分压反推出的 NTC 电阻，单位 Ω
    float kelvin;                                          // Beta 公式计算出的绝对温度，单位 K
    float celsius_x10;                                     // 摄氏温度放大 10 倍后的中间值

    if (adc_raw == 0U || adc_raw >= 4095U)                 // ADC 到达边界时无法得到有效电阻
        return 0U;                                         // 无效输入按 0.0℃ 处理

    resistance = NTC_FIXED_RESISTOR_OHM * (float)adc_raw /
                 (float)(4095U - adc_raw);                 // Rntc=Rfixed×ADC/(4095-ADC)
    kelvin = 1.0f / ((1.0f / NTC_T25_K) +
                     (logf(resistance / NTC_R25_OHM) / NTC_BETA)); // Beta 模型
    celsius_x10 = (kelvin - 273.15f) * 10.0f;               // 开尔文转摄氏度并放大 10 倍

    if (celsius_x10 < 0.0f)                                // 限制最低保存值
        celsius_x10 = 0.0f;
    if (celsius_x10 > 800.0f)                              // 限制最高保存值为 80.0℃
        celsius_x10 = 800.0f;

    return (uint16_t)celsius_x10;                           // 转为整数，供寄存器、日志和报警使用
}

/**
 * @brief  初始化 RTC 和 FatFs 存储
 * @param  无
 * @return 无
 * @note   SD 卡或文件系统失败时只关闭日志存储，不影响其他任务和 Modbus 通信。
 */
void log_storage_init(void)
{
    FRESULT result;

    if (FATFS_LinkDriver(&SD_Driver, g_fatfs_path) != 0U)          // 注册 SD 逻辑盘
    {
        printf("FatFs link driver failed\r\n");                   // 驱动表已满或注册失败
        return;
    }
    result = f_mount(&g_fatfs, g_fatfs_path, 1U);                  // 立即挂载 FAT 文件系统
    if (result == FR_OK)
    {
        g_log_storage_ready = 1U;                                  // 后续 LogTask 可以写文件
        printf("FatFs mounted: %s\r\n", g_fatfs_path);           // 输出盘符，便于确认 SD 已就绪
    }
    else
    {
        printf("FatFs mount failed: %d\r\n", (int)result);        // 失败只报告，不阻塞系统
        printf("SD HAL error: 0x%08lX\r\n",                         // 输出 SD 初始化阶段的 HAL 错误位
               (unsigned long)HAL_SD_GetError(&uSdHandle));
    }

    RTC_CLK_Config();                                              // SD 挂载完成后再配置 LSE 和 RTC
    RTC_TimeAndDate_Init();                                        // 首次上电写入默认时间
}


/* ════════════════════════════════════════════════════════════
 * vApplicationGetIdleTaskMemory — 提供空闲任务的静态 TCB 和栈
 *
 * 原型：void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer,
 *         StackType_t **ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize)
 *       ppxIdleTaskTCBBuffer   — [输出] 空闲任务 TCB 地址
 *       ppxIdleTaskStackBuffer — [输出] 空闲任务栈地址
 *       pulIdleTaskStackSize   — [输出] 空闲任务栈大小，单位为 StackType_t
 * 返值：无
 *
 * 调用示例：
 *   vTaskStartScheduler();  // 调度器内部自动调用本函数
 * ════════════════════════════════════════════════════════════ */
void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer,
                                   StackType_t **ppxIdleTaskStackBuffer,
                                   uint32_t *pulIdleTaskStackSize)
{
    *ppxIdleTaskTCBBuffer = &xIdleTaskTCB;             // 返回静态 TCB 地址
    *ppxIdleTaskStackBuffer = uxIdleTaskStack;         // 返回静态栈首地址
    *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;  // 返回栈深度
}

/* ════════════════════════════════════════════════════════════
 * vApplicationGetTimerTaskMemory — 提供软件定时器任务的静态内存
 *
 * 原型：void vApplicationGetTimerTaskMemory(StaticTask_t **ppxTimerTaskTCBBuffer,
 *         StackType_t **ppxTimerTaskStackBuffer, uint32_t *pulTimerTaskStackSize)
 *       ppxTimerTaskTCBBuffer   — [输出] Timer Task TCB 地址
 *       ppxTimerTaskStackBuffer — [输出] Timer Task 栈地址
 *       pulTimerTaskStackSize   — [输出] Timer Task 栈大小
 * 返值：无
 *
 * 调用示例：
 *   vTaskStartScheduler();  // configUSE_TIMERS=1 时由调度器自动调用
 * ════════════════════════════════════════════════════════════ */
void vApplicationGetTimerTaskMemory(StaticTask_t **ppxTimerTaskTCBBuffer,
                                    StackType_t **ppxTimerTaskStackBuffer,
                                    uint32_t *pulTimerTaskStackSize)
{
    *ppxTimerTaskTCBBuffer = &xTimerTaskTCB;                         // 返回静态 TCB 地址
    *ppxTimerTaskStackBuffer = uxTimerTaskStack;                     // 返回静态栈首地址
    *pulTimerTaskStackSize = configTIMER_TASK_STACK_DEPTH;           // 返回栈深度
}

/* ════════════════════════════════════════════════════════════
 * freertos_demo — 创建阶段 3 的四个任务并启动调度器
 *
 * 原型：void freertos_demo(void)
 * 形参：无
 * 返值：无；调度器正常启动后不会返回
 *
 * 原理：先建立寄存器互斥锁和四个业务任务，再启动 FreeRTOS 调度器。
 *
 * 调用示例：
 *   freertos_demo();  // main() 完成全部硬件初始化后调用
 * ════════════════════════════════════════════════════════════ */
void freertos_demo(void)
{
    BaseType_t result;																									      // FreeRTOS API 返回值
    modbus_registers_init(&g_registers);                                      // 初始化寄存器，默认值 holding[0]=1000，holding[1] = 200 holding[2] = 400 ，holding[3] = 0
	config_storage_load_holding(g_registers.holding);                         	// 阶段 7：优先加载 Flash 中的有效配置，第一次上电应该是用的默认配置
    RegisterMutex = xSemaphoreCreateMutex();                         			    // 创建寄存器互斥锁
    configASSERT(RegisterMutex != NULL);																	    // 创建失败则触发断言
		
		LogQueue = xQueueCreate(LOG_QUEUE_LENGTH, sizeof(log_record_t)); 					// 创建采集快照队列
		configASSERT(LogQueue != NULL);                                  					// 队列创建失败则触发断言

		/*1：创建 Modbus 通信任务*/
    result = xTaskCreate(modbus_task, "ModbusTask", MODBUS_TASK_STACK_SIZE,    
                         NULL, MODBUS_TASK_PRIORITY, &ModbusTaskHandle);			// 创建失败则触发断言
    configASSERT(result == pdPASS);
		
		/*2：创建监控任务*/
    result = xTaskCreate(monitor_task, "MonitorTask", MONITOR_TASK_STACK_SIZE,
                         NULL, MONITOR_TASK_PRIORITY, &MonitorTaskHandle);
    configASSERT(result == pdPASS);
		
		/*3： 创建数据采集任务*/
    result = xTaskCreate(acquire_task, "AcquireTask", ACQUIRE_TASK_STACK_SIZE,
                         NULL, ACQUIRE_TASK_PRIORITY, &AcquireTaskHandle);
    configASSERT(result == pdPASS);
		
		/*4： 创建日志任务*/
    result = xTaskCreate(log_task, "LogTask", LOG_TASK_STACK_SIZE,
                         NULL, LOG_TASK_PRIORITY, &LogTaskHandle);
    configASSERT(result == pdPASS);

    vTaskStartScheduler();                                           // 启动调度，随后由最高优先级就绪任务运行

    for (;;)                                                         // 只有堆不足导致调度器启动失败才会到这里
    {
    }
}

/* ════════════════════════════════════════════════════════════
 * modbus_task_notify_from_isr — 在 T3.5 中断中唤醒 ModbusTask
 *
 * 原型：void modbus_task_notify_from_isr(void)
 * 形参：无
 * 返值：无
 *
 * 原理：用任务通知代替主循环轮询；若唤醒更高优先级任务，退出 ISR 前立即调度。
 *
 * 调用示例：
 *   modbus_task_notify_from_isr();  // TIM4 确认完整帧后调用

	 运行流程
	 higherPriorityTaskWoken = pdFALSE
             ↓
	 vTaskNotifyGiveFromISR()
             ↓
	是否唤醒了更高优先级任务？
        ├─ 是 → 修改为 pdTRUE
        └─ 否 → 保持 pdFALSE
             ↓
	portYIELD_FROM_ISR()
        ├─ TRUE → 中断退出后切换任务
        └─ FALSE → 继续原来的任务
				
			
		TIM4 中断唤醒 ModbusTask 后，只要当时没有更高优先级的就绪任务，并且中断前运行的任务优先级低于 ModbusTask，退出中断后就会立即切换到 ModbusTask。
		FreeRTOS 任务优先级：
		MonitorTask  4
		ModbusTask   3
		AcquireTask  2
		LogTask      1
		
 * ════════════════════════════════════════════════════════════ */
void modbus_task_notify_from_isr(void)
{
    BaseType_t higherPriorityTaskWoken = pdFALSE;													//这个变量用于记录：本次中断通知是否唤醒了一个比当前任务优先级更高的任务
																																					// 先假设：没有更高优先级任务被唤醒
    if (ModbusTaskHandle != NULL && g_modbus_task_ready != 0U)						//满足：存在ModbusTask任务句柄和modbus_task_ready =1两个条件
    {
        vTaskNotifyGiveFromISR(ModbusTaskHandle, &higherPriorityTaskWoken); // 在中断里给 ModbusTask 发一个通知，告诉它“有数据/完整帧到了”。
        portYIELD_FROM_ISR(higherPriorityTaskWoken);                      //定时器中断调用了modbus_task_notify_from_isr()函数唤醒了ModbusTask任务
    }																																			//如果此时没有比ModbusTask任务优先级更高的任务处于就绪态，
																																					//那么定时器中断结束后立刻切换过去运行它
		
}

/* ════════════════════════════════════════════════════════════
 * adc_dma_notify_from_isr — DMA 完成时唤醒 AcquireTask
 *
 * 原型：void adc_dma_notify_from_isr(void)
 * 形参：无；返值：无
 * 原理：DMA 回调运行在中断上下文，只发送任务通知，不做求平均、浮点换算
 *       或寄存器访问，把耗时工作留给 AcquireTask。
 * ════════════════════════════════════════════════════════════ */
void adc_dma_notify_from_isr(void)
{
    BaseType_t higherPriorityTaskWoken = pdFALSE;

    if (AcquireTaskHandle != NULL && g_acquire_task_ready != 0U)
    {
        vTaskNotifyGiveFromISR(AcquireTaskHandle, &higherPriorityTaskWoken);
        portYIELD_FROM_ISR(higherPriorityTaskWoken);
    }
}

/* ════════════════════════════════════════════════════════════
 * modbus_task — 等待完整帧通知，解析请求并发送响应
 *xTaskCreate(modbus_task,
 *           "ModbusTask",
 *           MODBUS_TASK_STACK_SIZE,
 *           NULL,                    // ← 这个就是 pvParameters
 *           MODBUS_TASK_PRIORITY,
 *           &ModbusTaskHandle);
 * 原型：static void modbus_task(void *pvParameters)
 *       pvParameters — [输入] 任务参数我们传入NULL
 * 返值：无；任务函数不会返回
 *
 * 原理：
 *   1. 平时阻塞等待 TIM4 的 T3.5 完整帧通知；
 *   2. 被唤醒后，在短临界区内复制 RS485 接收帧并清除接收状态；
 *   3. 使用互斥锁取得实时寄存器快照；
 *   4. 在锁外调用纯 C Modbus 协议层；
 *   5. 如果保持寄存器发生变化，再加锁一次性提交；
 *   6. 协议层要求回复时，通过 RS485 发送响应帧。
 * ════════════════════════════════════════════════════════════ */
static void modbus_task(void *pvParameters)
{
		
    modbus_registers_t registerSnapshot;													 /*协议层使用的寄存器副本：协议解析期间操作这个快照，不长期占用共享寄存器互斥锁*/
    uint16_t holdingBefore[MODBUS_HOLDING_REGISTER_COUNT];				 /* 保存协议处理前的保持寄存器，处理结束后与 registerSnapshot.holding 比较，判断本次请求是否真正修改了保持寄存器。*/

    (void)pvParameters;																						 /* 本任务没有使用 xTaskCreate() 传入的任务参数，转换为 void 可以避免编译器产生“参数未使用”警告*/
    g_modbus_task_ready = 1U;                                      // 任务已运行，允许 TIM4 发送 FromISR 通知
																																	 /*
																																	 ModbusTaskHandle != NULL 表示→ 任务对象已经成功创建
																																	 g_modbus_task_ready != 0 表示→ 任务函数已经真正开始执行，才会发送任务通知。
																																	 */
	
	
		/* FreeRTOS 任务通常使用无限循环持续运行。 */
    for (;;)
    {
        uint16_t requestLength;																			 // 本次完整请求帧的实际字节数
			  uint8_t requestValid;																				 // T1.5 检查结果：1=连续，0=帧内曾超时，为无效帧
        size_t responseLength = 0U;																	 // 协议层生成的响应帧长度
        modbus_rtu_status_t status;																	 // 协议层处理结果：回复、无回复或错误

				/*在当前没有任务通知时，会让 ModbusTask 从运行态进入阻塞态：*/
        if (modbus_transport_frame_ready() == 0U)										 //情况1：没有完整帧时，ModbusTask 会阻塞在 ulTaskNotifyTake()这一行，进入阻塞态，而不是退出，完整帧到达并发来通知后，任务从这一行醒来，继续执行后面的复制、解析和回复代码  
					ulTaskNotifyTake(pdTRUE, portMAX_DELAY);                   //情况2：完整帧已经提前在任务进入就态绪时，这个if不会进入，因为返回的frame_ready已经是1了
																																		 //ulTaskNotifyTake的第一个形参，指的是TCB的结构体中的变量成员任务通知值，每次ulTaskNotifyTake执行一次，将任务通知值清0，第二个参数是使任务无限期进入阻塞态
				
				/*TIM4 中断发出事件，唤醒 ModbusTask后*/
        taskENTER_CRITICAL();                                         // BASEPRI 屏蔽 USART2/TIM4中断，保护共享接收区，临界区内只进行少量复制和赋值，不做协议解析和串口发送
        requestLength = bsp_485_rx_len;                               // 保存本次请求实际长度
				
				/* 二次限制请求长度，防止复制超过 g_request[256] 的范围。 */
        if (requestLength > MODBUS_MAX_ADU_SIZE)
            requestLength = MODBUS_MAX_ADU_SIZE;                      // 二次限制 ADU 最大 256 字节
				
				/* 把中断接收缓冲区复制到 ModbusTask 的私有请求缓冲区*/
        memcpy(g_request, (const void *)bsp_485_rx_buf, requestLength); // 将 ISR 接收区复制到任务私有区g_request
        requestValid = modbus_transport_frame_valid();                // 清状态前保存 T1.5 检查结果
        bsp_485_rx_len = 0U;                                         // 释放 ISR 接收缓冲区
        modbus_transport_frame_clear();                              // 为下一帧恢复传输状态
        taskEXIT_CRITICAL();                                         // 恢复被 FreeRTOS 临界区屏蔽的中断

        if (requestValid == 0U)
            continue;                                                // 帧内间隔异常，静默丢弃，continue 会跳回 for (;;) 开头，重新等待下一帧

				/*只要 g_registers 是多个任务共享的数据，那么所有会访问它的任务都应该遵守同一套规则
				  获取锁 → 复制数据 → 保存副本 → 释放锁
				*/
				xSemaphoreTake(RegisterMutex, portMAX_DELAY); // 获取寄存器表互斥锁；若已被其他任务占用，则阻塞等待
				registerSnapshot = g_registers;              // 持锁期间复制共享寄存器表，保证快照数据一致
				memcpy(holdingBefore, registerSnapshot.holding, sizeof(holdingBefore));
				xSemaphoreGive(RegisterMutex);               // 复制完成，立即释放寄存器表互斥锁

				
				/*调用阶段 1 完成的纯 C Modbus 协议层。
					协议层会检查长度、CRC、从站地址、功能码、寄存器地址、广播规则等，并返回是否需要发送响应。
				*/
        status = modbus_rtu_process_request(g_request, requestLength,
                                            &registerSnapshot,
                                            g_response, sizeof(g_response),
                                            &responseLength);        
				/* 比较处理前后的保持寄存器。
					 合法的0x03/0x04 读请求不会改变 holding[]，memcmp() 返回 0。	
					 合法的 0x06/0x10 写请求会改变 holding[]，memcmp() 返回非 0。																			
				*/
        if (memcmp(holdingBefore, registerSnapshot.holding, sizeof(holdingBefore)) != 0) //非 0  → 内容不同，保持寄存器发生了修改
        {
            xSemaphoreTake(RegisterMutex, portMAX_DELAY);             // 合法写请求完成后再短暂上锁
            memcpy(g_registers.holding, registerSnapshot.holding,			//现在短暂获取互斥锁，把结果一次性提交到实时寄存器。
                   sizeof(g_registers.holding));                     
            xSemaphoreGive(RegisterMutex);
            config_storage_save_holding(registerSnapshot.holding);    // 保持寄存器变化后保存到 W25Q256
        }

        g_modbus_heartbeat++;                                        // 记录 ModbusTask 已完成一轮处理
				
				/*协议层明确返回 RESPONSE_READY 时才发送响应*/
        if (status == MODBUS_RTU_RESPONSE_READY)
            BSP_485_SendBuf(g_response, (uint16_t)responseLength);    // 等待 TC 后切回接收
    }
}

/* ════════════════════════════════════════════════════════════
 * monitor_task — 以固定 100ms 周期记录系统监控心跳
 *
 * 原型：static void monitor_task(void *pvParameters)
 *       pvParameters — [输入] 任务参数，本任务未使用
 * 返值：无；任务函数不会返回
 *
 * 原理：vTaskDelayUntil 使用绝对唤醒时刻，避免周期随执行时间逐步漂移。
 *
 * 调用示例：
 *   xTaskCreate(monitor_task, "MonitorTask", 128, NULL, 4, &handle);
* 作用
	MonitorTask 每 100ms 醒来一次，检查关键任务有没有持续推进；如果健康就喂 IWDG，
	如果发现关键任务持续异常就停止喂狗，让 IWDG 最终复位 MCU，
	同时把诊断结果写入 Modbus 的 input[8]~input[11]。
  它主要负责：监测 → 判断 → 记录 → 决定是否喂狗 → 上报
 * ════════════════════════════════════════════════════════════ */
static void monitor_task(void *pvParameters)
{
    TickType_t lastWakeTime = xTaskGetTickCount();								 //获取当前 FreeRTOS 系统 Tick 计数。
															   //先在地上做一个起点标记，vTaskDelayUntil() 能保持固定任务周期的原因。
    (void)pvParameters;																						//避免编译器报告“参数未使用”的警告。

    for (;;)
    {
        uint8_t health_ok = 1U;                                      // 注意这个变量是本轮结果，每 100 ms 进入循环时都会重新初始化为 1
																																		 //如果后面发现 AcquireTask 或 LogTask 异常，再改成：0
				/*第一轮开始，程序进入这里。*/
        if (g_health_snapshot_valid == 0U)                          // 是否已经保存过上一轮的心跳值，程序刚启动时，全局变量默认是 0，所以第一次会进入这里。
        {
						g_last_acquire_heartbeat = g_acquire_heartbeat;          // 保存采集任务当前心跳，g_acquire_heartbeat初始值为0
						g_last_log_heartbeat = g_log_heartbeat;                  // 保存日志任务当前心跳，g_log_heartbeat初始值为0
            g_health_snapshot_valid = 1U;                            // 后续轮次开始正式比较
        }
				/*从第二轮开始，程序进入这里。*/
        else
        {
            if (g_acquire_heartbeat == g_last_acquire_heartbeat)     // 当前 AcquireTask 心跳和上一次保存的值一样
            {
							if (g_acquire_stall_ticks < 0xFFU)                   // g_acquire_stall_ticks是AcquireTask不工作时+1
                    g_acquire_stall_ticks++;											 //0xFFU是值255，怕（uint8_t）g_acquire_stall_ticks溢出
            }
            else
                g_acquire_stall_ticks = 0U;                         // 采集任务恢复推进，清除连续停滞计数

            if (g_acquire_stall_ticks >= 20U)                        // 连续 20 个监测周期没有看到 AcquireTask 心跳变化。
                health_ok = 0U;																			 //发现AcquireTask异常，健康值设置0，一会写入input[11]
						
						/*
						条件一：g_log_heartbeat == g_last_log_heartbeat，表示LogTask 的心跳没有变化，这个任务不工作了
						条件二：uxQueueMessagesWaiting(LogQueue) > 0U 表示LogQueue 中还有数据等待处理。
						为什么必须检查队列有数据？ 因为如果队列为空，LogTask 阻塞等待是正常行为，不能认为它卡死。
						只有队列里有数据，但 LogTask 没有消费，才可能表示 LogTask 停滞。
						*/
            if (g_log_heartbeat == g_last_log_heartbeat &&
                uxQueueMessagesWaiting(LogQueue) > 0U)               // 比较日志任务心跳并检查队列
            {
                if (g_log_stall_ticks < 0xFFU)                       // 和 AcquireTask 一样，防止 8 位计数器超过 255 后溢出。
                    g_log_stall_ticks++;
            }
            else
                g_log_stall_ticks = 0U;                              // 日志任务恢复运行，清除连续停滞计数
					
						
						/*
						条件一：表示 SD/FatFs 已经成功挂载。
						条件二：有日志积压，并且连续约 2 秒没有被 LogTask 消费。
						如果 SD 根本没有挂载，就不使用这条“日志停滞”判断。
						*/
            if (g_log_storage_ready != 0U && g_log_stall_ticks >= 20U) // SD 已挂载且队列有积压、连续 2 秒无消费才判故障
                health_ok = 0U;

            g_last_acquire_heartbeat = g_acquire_heartbeat;          // g_acquire_heartbeat在acquire_task采集任务每次采集后+1，此处第二次进入监测函数时，把1赋值给g_last_acquire_heartbeat
            g_last_log_heartbeat = g_log_heartbeat;                  // g_last_log_heartbeatlog_task日志任务每次完成后+1，此处第二次进入监测函数时，把1赋值给g_last_log_heartbeat
        }

        if (health_ok != 0U)                                          // 关键任务都在运行
        {
            IWDG_Feed();                                             // 健康时喂狗，避免无故复位
        }
        else
        {
            g_health_fault_count++;                                  // 每次 MonitorTask 发现本轮不健康，就增加一次。
        }
			
        xSemaphoreTake(RegisterMutex, portMAX_DELAY);                // 获取寄存器互斥锁
        modbus_registers_update_input(&g_registers, 8U,
                                     (uint16_t)g_reset_cause);        // input[8]：最近一次复位原因位图
        modbus_registers_update_input(&g_registers, 9U,
                                     (uint16_t)g_health_fault_count); // input[9]：心跳故障次数
        modbus_registers_update_input(&g_registers, 10U,
                                     (uint16_t)(g_log_drop_count + g_log_write_error_count)); // input[10]：日志丢弃及写入错误总数
        modbus_registers_update_input(&g_registers, 11U,
                                     (uint16_t)(health_ok != 0U));    // input[11]：本轮健康状态，1=正常
        xSemaphoreGive(RegisterMutex);                               // 释放诊断寄存器锁

        g_monitor_heartbeat++;                                       // 当前只用于观察自身是否继续运行，没有写入input寄存器
        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(MONITOR_PERIOD_MS)); // 保持严格 100ms 周期
    }
}

/**
 * @brief  ADC 数据采集任务
 * @param  pvParameters 任务参数，本任务未使用
 * @return 无；任务函数不会返回
 *
 * @原理：
 *         DMA 持续采集 PC3 和 PA4 两路 ADC 数据。
 *         DMA 完成半缓冲区或整缓冲区后，通过任务通知唤醒本任务。
 *         本任务复制已经完成的缓冲区，分别计算两路 ADC 平均值，
 *         再进行工程量换算、温度换算、报警判断和寄存器更新。
 *
 * @调用示例：
 *         xTaskCreate(acquire_task, "AcquireTask",
 *                     ACQUIRE_TASK_STACK_SIZE, NULL,
 *                     ACQUIRE_TASK_PRIORITY, &AcquireTaskHandle);
 */
static void acquire_task(void *pvParameters)
{
    uint16_t sample_copy[ADC_BUF_SIZE / 2U];													// 保存 DMA 已完成半区的数据，避免直接读取正在改写的缓冲区
    const uint16_t samples_per_channel = ADC_SAMPLES_PER_BATCH / 2U;	//它表示：当前处理的半区中，每个通道各有 16 次采样。

    (void)pvParameters;																								// 本任务没有使用创建任务时传入的参数，避免爆警告
    g_acquire_task_ready = 1U; 																				// 任务真正开始运行后，DMA 中断才允许发送通知

    for (;;)
    {
        uint8_t ready_flags;																					// 保存 DMA 半区完成标志
        uint32_t pot_sum = 0U;																				// 用于保存电位器 ADC 累加值
        uint32_t ntc_sum = 0U;																				// 用于保存 NTC ADC 累加值
        uint16_t pot_raw;																							// 用于保存 电位器平均 ADC 原始值
        uint16_t ntc_raw; 																						// 用于保存NTC 平均 ADC 原始值
        uint16_t pot_x10;																						  // 电位器工程量
        uint16_t ntc_temp_x10;																				// NTC 温度，放大 10 倍保存
        uint16_t alarm_status;																				// 报警状态
        uint16_t sample_period_ms;																		// 采集任务处理周期
        uint16_t alarm_low_x10;																				// 报警下限，单位为 0.1℃
        uint16_t alarm_high_x10;																			// 报警上限，单位为 0.1℃

        /* 没有 DMA 完成事件时阻塞，不占用 CPU。 */
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));									// 取出任务通知，并清除通知计数
																																			// 最多等 100ms，收到通知就立即醒来，没收到就超时返回。
        /* 复制刚刚完成的半区，避免 DMA 正在改写时直接计算。 */
        taskENTER_CRITICAL();																					// 临时保护 DMA 缓冲区和完成标志
        ready_flags = adc_buf_ready;																	// 读取 DMA 当前完成的是哪一半，1是前半段，2是后半段
        adc_buf_ready = 0U;																						// 清除本次完成标志，等待下一次 DMA 事件

        if ((ready_flags & 2U) != 0U)																	// bit1=1：DMA 后半区已经完成
        {
            memcpy(sample_copy, &adc_buf[ADC_BUF_SIZE / 2U],					// 将后半区复制到任务自己的临时数组
                   sizeof(sample_copy));
        }
        else if ((ready_flags & 1U) != 0U)														// bit0=1：DMA 前半区已经完成
        {
            memcpy(sample_copy, adc_buf, sizeof(sample_copy));				// 将前半区复制到任务自己的临时数组
        }
        else
        {
            taskEXIT_CRITICAL();																			// 没有有效 DMA 数据，先恢复中断
            continue;																									// 回到for循环，重新等待下一次通知
        }	
        taskEXIT_CRITICAL();																					// 数据复制完成，恢复被临时屏蔽的中断

        for (uint16_t index = 0U; index < samples_per_channel; ++index)// 遍历当前半区中的每一组采样，通道数ADC_CH_COUNT=2
        {
            pot_sum += sample_copy[index * ADC_CH_COUNT];       			// Rank1：取 PC3 电位器数据累加
            ntc_sum += sample_copy[index * ADC_CH_COUNT + 1U];   			// Rank2：取 PA4 NTC 数据累加
        }
        pot_raw = (uint16_t)(pot_sum / samples_per_channel);					// 计算电位器 ADC 平均值，总值pot_sum/16
        ntc_raw = (uint16_t)(ntc_sum / samples_per_channel);					// 计算 NTC ADC 平均值，总值ntc_raw/16
        pot_x10 = (uint16_t)(((uint32_t)pot_raw * 1000U) / 4095U);		// 电位器AO值0~4095 换算为 0~1000

        ntc_temp_x10 = ntc_adc_to_temp_x10(ntc_raw);       // 使用 10K/10K/B3950 参数换算温度

        /* 读取配置和更新输入寄存器必须使用同一把互斥锁。 */
        xSemaphoreTake(RegisterMutex, portMAX_DELAY);									// 获取寄存器互斥锁，保护共享寄存器
        sample_period_ms = g_registers.holding[0];										// 读取采集任务处理周期
        alarm_low_x10 = g_registers.holding[1];												// 读取温度报警下限
        alarm_high_x10 = g_registers.holding[2];											 // 读取温度报警上限
        if (alarm_low_x10 > alarm_high_x10)														 // 下限大于上限，配置顺序错误
            alarm_status = 3U;                 												  // 3=报警配置错误
        else if (ntc_temp_x10 < alarm_low_x10)													// 当前温度低于报警下限
            alarm_status = 1U;                 													// 1=低温报警
        else if (ntc_temp_x10 > alarm_high_x10)													// 当前温度高于报警上限
            alarm_status = 2U;               														// 2=高温报警
        else
            alarm_status = 0U;                 													// 0=温度处于正常范围

        modbus_registers_update_input(&g_registers, 0U, pot_raw);			  // input[0]写入电位器原始值
        modbus_registers_update_input(&g_registers, 1U, pot_x10);				// input[1]写入电位器工程量
        modbus_registers_update_input(&g_registers, 2U, ntc_raw);				// input[2]写入NTC原始值
        modbus_registers_update_input(&g_registers, 3U, ntc_temp_x10);	// input[3]写入温度×10
        modbus_registers_update_input(&g_registers, 4U, alarm_status);	 // input[4]写入报警状态
        xSemaphoreGive(RegisterMutex);

        /* USART1 调试输出：不用浮点 printf，×10 数值手动拆成整数和小数。 */
        printf("ADC pot_raw=%u, pot=%u.%u%%, ntc_raw=%u, temp=%u.%uC, alarm=%u\r\n",
               (unsigned)pot_raw,																							// 输出电位器 ADC 原始值
               (unsigned)(pot_x10 / 10U), (unsigned)(pot_x10 % 10U),					// 输出电位器工程量
               (unsigned)ntc_raw,																							// 输出 NTC ADC 原始值											
               (unsigned)(ntc_temp_x10 / 10U), (unsigned)(ntc_temp_x10 % 10U),// 输出温度工程值
               (unsigned)alarm_status);																				// 输出报警状态

        {
            log_record_t record;                              								// 本次采集的日志快照对应结构体：记录时间和采样值的
            HAL_RTC_GetTime(&Rtc_Handle, &record.time, RTC_FORMAT_BIN); 			// 读取时间
            HAL_RTC_GetDate(&Rtc_Handle, &record.date, RTC_FORMAT_BIN); 			// 紧接着读取日期
            record.pot_raw = pot_raw;                          								// 保存电位器原始值
            record.pot_x10 = pot_x10;                          								// 保存电位器工程量
            record.ntc_raw = ntc_raw;                          								// 保存 NTC 原始值
            record.ntc_temp_x10 = ntc_temp_x10;                								// 保存温度工程值
            record.alarm_status = alarm_status;                								// 保存报警状态
            if (xQueueSend(LogQueue, &record, 0U) != pdPASS)    							// 发送采集数据到队列，队列满时不阻塞采集任务
                g_log_drop_count++;                            								// 记录丢弃数量
        }

							 
				/*
				每成功处理一批 ADC 数据，就加 1。
				它不是 ADC 数据，而是一个运行状态计数器，后面的 MonitorTask 可以通过观察它是否继续增加，判断 AcquireTask 有没有卡死
				*/
        g_acquire_heartbeat++;																								 // 记录采集任务成功处理了一次数据
        if (sample_period_ms == 0U)																						 // 这里判断它是否被设置成了 0。虽然寄存器写入函数已经禁止 holding[0] 写 0，但这里仍然再保护一次。
            sample_period_ms = ACQUIRE_PERIOD_MS;															 // 如果发现周期是 0，就恢复默认值100ms采样
        vTaskDelay(pdMS_TO_TICKS(sample_period_ms));													 // AcquireTask 进入阻塞态约 1000ms，把 CPU 让给其他任务。延时结束后，它会回到 for (;;) 循环，继续等待下一次 ADC DMA 通知。
    }
}

/* ════════════════════════════════════════════════════════════
 * log_task — 从日志队列取出快照并写入 SD 卡 CSV 文件
 *
 * 原型：static void log_task(void *pvParameters)
 *       pvParameters — [输入] 任务参数，本任务未使用
 * 返值：无；任务函数不会返回
 *
 * 原理：AcquireTask 是生产者，LogTask 是消费者；SD/FatFs 写入只在本任务中执行，
 *       SD 缺失或挂载失败时不影响 Modbus 和 ADC 采集任务。
 *
 * 调用示例：
 *   xTaskCreate(log_task, "LogTask", 128, NULL, 1, &handle);
 * ════════════════════════════════════════════════════════════ */
static void log_task(void *pvParameters)
{
    log_record_t record;									// 保存从队列取出的单条采样快照
    FIL file;															// FatFs 文件对象，代表当前打开的文件
    UINT written;													// 保存 f_write 实际写入的字节数
    static uint8_t header_written;				// 静态变量只初始化一次，用于记录表头是否写过
    (void)pvParameters;										// 本任务不使用创建参数，避免编译器产生未使用警告

    for (;;)															// 任务永久循环运行，不允许任务函数正常返回
    {
        if (xQueueReceive(LogQueue, &record, portMAX_DELAY) != pdPASS) // 从日志队列接收一条采样记录；队列为空时永久阻塞等待
            continue;																									 // 接收失败则跳过本轮，回到for循环开头等待下一条日志记录

        if (g_log_storage_ready != 0U)                								 // 检查 SD 卡存储是否已经初始化并挂载成功
        {
            if (f_open(&file, "0:/measure.csv", FA_OPEN_APPEND | FA_WRITE) == FR_OK)// 以追加写模式打开 CSV 文件
            {
                if (header_written == 0U)																						// 如果 CSV 表头还没有写入
                {
                    const char *header = "date,time,pot_raw,pot_x10,ntc_raw,temp_x10,alarm\r\n";// 定义 CSV 第一行字段名称
                    if (f_write(&file, header, (UINT)strlen(header), &written) == FR_OK &&	//将表头字符串写入文件，并检查 FatFs 写操作是否成功
                        written == (UINT)strlen(header))																		// 检查实际写入字节数是否与表头长度完全一致，防止只写入部分数据
                        header_written = 1U;                    														// 表头完整写入后设置标志，后续日志不再重复写表头
                }
                if (f_printf(&file, "20%02u-%02u-%02u,%02u:%02u:%02u,%u,%u,%u,%u,%u\r\n", // 按 CSV 格式写入一条采样记录
                         record.date.Year, 																									// 写入年份的后两位，例如 26 表示 2026
												 record.date.Month, 																								// 写入月份
												 record.date.Date,																									// 写入日期
                         record.time.Hours, 																								// 写入小时
								         record.time.Minutes, 																							// 写入分钟
								         record.time.Seconds,																								// 写入秒
                         record.pot_raw, 																										// 写入电位器 ADC 原始采样值
												 record.pot_x10, 																										// 写入经过换算的电位器工程值
												 record.ntc_raw,																										// 写入 NTC ADC 原始采样值
                         record.ntc_temp_x10, 																							// 写入 NTC 温度值，通常按 ×10 保存
												 record.alarm_status) < 0)                          
								
								/*  f_printf 返回负数表示格式化写入失败，g_log_write_error_count++*/
								
                    g_log_write_error_count++;             															// 记录 SD 文件写入故障，供诊断寄存器input[10]读取
								
                if (f_close(&file) != FR_OK)                                                              // 关闭文件时也可能提交失败
                    g_log_write_error_count++;                                                            // 记录关闭/提交阶段的存储故障
            }
            else
                g_log_write_error_count++;                                                                // 文件打开失败，记录一次 SD 写入故障
        }
        else
        {
            for (volatile uint32_t load = 0U; load < LOG_SIMULATED_LOAD_COUNT; ++load) { } // SD 不可用时执行一小段模拟负载，模拟日志任务仍然在运行
        }

        g_last_logged_input = record.pot_raw;                     // 保存本次成功从日志队列取出的快照中的电位器原始值
        g_log_heartbeat++;                                        // 日志任务心跳计数加一，用于监控 LogTask 是否仍在正常运行
    }
}
