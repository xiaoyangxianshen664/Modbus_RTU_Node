/**
  ******************************************************************************
  * @file    freertos_demo.c
  * @brief   阶段 3 FreeRTOS 四任务应用
  * @note    ModbusTask 处理通信，MonitorTask 监控周期，
  *          AcquireTask 模拟采集，LogTask 模拟低优先级日志负载。
  ******************************************************************************
  */

#include "freertos_demo.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "./485/bsp_485.h"
#include "modbus_transport.h"
#include "modbus_rtu.h"
#include "modbus_registers.h"
#include <string.h>
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
#define ACQUIRE_TASK_STACK_SIZE   128U  // AcquireTask 栈：128 word，约 512B
#define LOG_TASK_STACK_SIZE       128U  // LogTask 栈：128 word，约 512B

/* ════════════════════════════════════════════════════════════
 * 周期任务的运行周期
 *
 * ModbusTask 不设置固定周期，而是一直阻塞等待完整帧通知。
 * 其他三个任务按照各自的周期运行。
 * ════════════════════════════════════════════════════════════ */

#define MONITOR_PERIOD_MS         100U   // MonitorTask 每 100ms 执行一次健康监测
#define ACQUIRE_PERIOD_MS         100U   // AcquireTask 每 100ms 更新一次模拟采集值
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
static volatile uint32_t g_acquire_heartbeat;  // AcquireTask 每完成一次模拟采集后加 1
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
static void log_task(void *pvParameters);      // 1s 周期获取寄存器快照并执行日志操作


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
    modbus_registers_init(&g_registers);                                      // 初始化寄存器，holding[0]=1000
    RegisterMutex = xSemaphoreCreateMutex();                         			    // 创建寄存器互斥锁
    configASSERT(RegisterMutex != NULL);																	    // 创建失败则触发断言
		
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
 * ════════════════════════════════════════════════════════════ */
static void monitor_task(void *pvParameters)
{
    TickType_t lastWakeTime = xTaskGetTickCount();

    (void)pvParameters;

    for (;;)
    {
        g_monitor_heartbeat++;                                       // 阶段 6 将在这里检查其他任务心跳并决定喂狗
        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(MONITOR_PERIOD_MS)); // 保持严格 100ms 周期
    }
}

/* ════════════════════════════════════════════════════════════
 * acquire_task — 周期生成模拟采集值并更新输入寄存器
 *
 * 原型：static void acquire_task(void *pvParameters)
 *       pvParameters — [输入] 任务参数，本任务未使用
 * 返值：无；任务函数不会返回
 *
 * 原理：阶段 3 用递增数代替 ADC；阶段 4 再替换为 DMA 采样和工程量换算。
 *
 * 调用示例：
 *   xTaskCreate(acquire_task, "AcquireTask", 128, NULL, 2, &handle);
 * ════════════════════════════════════════════════════════════ */
static void acquire_task(void *pvParameters)
{
    TickType_t lastWakeTime = xTaskGetTickCount();
    uint16_t simulatedValue = 0U;

    (void)pvParameters;

    for (;;)
    {
        simulatedValue++;                                            // 模拟一个持续变化的采集量

        xSemaphoreTake(RegisterMutex, portMAX_DELAY);                 // 保护共享寄存器
        modbus_registers_update_input(&g_registers, 0U, simulatedValue); // 将模拟值映射到 input[0]
        xSemaphoreGive(RegisterMutex);

        g_acquire_heartbeat++;
        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(ACQUIRE_PERIOD_MS)); // 每 100ms 更新一次
    }
}

/* ════════════════════════════════════════════════════════════
 * log_task — 获取寄存器快照并模拟低优先级日志负载
 *
 * 原型：static void log_task(void *pvParameters)
 *       pvParameters — [输入] 任务参数，本任务未使用
 * 返值：无；任务函数不会返回
 *
 * 原理：锁内只复制快照，耗时模拟放在锁外；阶段 5 再换成 RTC + SD/FatFs。
 *
 * 调用示例：
 *   xTaskCreate(log_task, "LogTask", 128, NULL, 1, &handle);
 * ════════════════════════════════════════════════════════════ */
static void log_task(void *pvParameters)
{
    TickType_t lastWakeTime = xTaskGetTickCount();
    modbus_registers_t registerSnapshot;

    (void)pvParameters;

    for (;;)
    {
        xSemaphoreTake(RegisterMutex, portMAX_DELAY);                 // 只在复制快照期间占用互斥锁
        registerSnapshot = g_registers;
        xSemaphoreGive(RegisterMutex);

        g_last_logged_input = registerSnapshot.input[0];             // 保存本轮模拟日志的数据
        for (volatile uint32_t load = 0U; load < LOG_SIMULATED_LOAD_COUNT; ++load)
        {
        }                                                            // 在锁外模拟较慢的 SD 写入工作

        g_log_heartbeat++;
        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(LOG_PERIOD_MS)); // 每 1s 执行一次模拟日志
    }
}
