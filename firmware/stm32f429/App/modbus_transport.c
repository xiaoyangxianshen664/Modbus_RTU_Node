#include "modbus_transport.h"
#include "bsp_485.h"

/* ── T3.5 静默定时器（TIM4）── */
TIM_HandleTypeDef htim4;

/* 帧完成标志：1 = 静默超过 T3.5，一帧收完待处理 */
static volatile uint8_t frame_ready = 0;
static volatile uint8_t frame_valid = 1;
static volatile uint8_t silence_ms = 0;

/* ── 定时器参数 ──
 * 定时器时钟 = 90MHz（APB1 定时器时钟，F429 @180MHz 系统时钟）
 * Prescaler 90-1  → 1MHz 计数，1 个计数 = 1μs
 * Period    1000-1 → 1ms 节拍
 * 约 2ms 视为超过 T1.5，约 5ms 视为超过 T3.5
 * ── */
#define T35_PRESCALER   (90U - 1U)
#define T35_PERIOD      (1000U - 1U)



/* ════════════════════════════════════════════════════════════
 * modbus_transport_init — 初始化 T3.5 静默定时器
 *
 * 原型：void modbus_transport_init(void)
 * 返值：无
 *
 * 原理：
 *   TIM4 每 1ms 产生一次节拍。每收 1 字节清零静默计数；
 *   静默超过 T1.5 后若又收到字节，说明帧内间隔异常；
 *   若继续静默到 T3.5，则属于正常帧结束。
 *
 * 调用示例：
 *   modbus_transport_init();  // main 里，BSP_485_Init 之后调
 * ════════════════════════════════════════════════════════════ */
void modbus_transport_init(void)
{
    __HAL_RCC_TIM4_CLK_ENABLE();          /* 使能 TIM4 外设时钟 */
    htim4.Instance         = TIM4;        /* 绑定 TIM4 硬件实例 */
    htim4.Init.Prescaler   = T35_PRESCALER;   /* 预分频 90-1 → 1MHz 计数频率（1μs/脉冲） */
    htim4.Init.Period      = T35_PERIOD;  /* 自动重装载 999 → 每 1ms 触发一次溢出中断 */
    htim4.Init.CounterMode = TIM_COUNTERMODE_UP; /* 向上计数模式 */
    HAL_TIM_Base_Init(&htim4);            /* 写入时基配置，完成 TIM4 初始化 */

    HAL_NVIC_SetPriority(TIM4_IRQn, 6, 0);    /* 优先级 6：允许调用 FreeRTOS FromISR API */
    HAL_NVIC_EnableIRQ(TIM4_IRQn);         /* 使能 NVIC 中 TIM4 的中断通道 */
    HAL_TIM_Base_Start_IT(&htim4);         /* 启动定时器并开启更新（溢出）中断 */
}

/* ════════════════════════════════════════════════════════════
 * modbus_transport_on_byte — 每收 1 字节调用，重置 T3.5 计时
 *
 * 原型：void modbus_transport_on_byte(void)
 * 返值：无
 *
 * 原理：
 *   先检查这个字节到来前的静默时间：若已超过 T1.5 但未到 T3.5，
 *   说明一帧中出现非法间隔；然后清零计时，重新等待下一字节。
 *
 * 调用示例：
 *   modbus_transport_on_byte();  // 在 UART 接收回调里，每收 1 字节调
 * ════════════════════════════════════════════════════════════ */
void modbus_transport_on_byte(void)
{
    if (silence_ms >= 2U && silence_ms < 5U)
        frame_valid = 0;                    /* T1.5~T3.5 内又来字节：帧内间隔超时，整帧作废 */

    __HAL_TIM_SET_COUNTER(&htim4, 0);       /* 硬件计数器清零，重新开始 T3.5 倒计时 */
    silence_ms = 0;                         /* 软件静默毫秒累加器同步清零 */
}

/* ════════════════════════════════════════════════════════════
 * modbus_transport_frame_ready — 查询一帧是否收完
 *
 * 原型：uint8_t modbus_transport_frame_ready(void)
 * 返值：1 = 帧收完待处理，0 = 还没收完/没数据
 *
 * 调用示例：
 *   if (modbus_transport_frame_ready()) { ... }
 * ════════════════════════════════════════════════════════════ */
uint8_t modbus_transport_frame_ready(void)
{
    return frame_ready;															//	static volatile uint8_t frame_ready = 0;

}

uint8_t modbus_transport_frame_valid(void)
{
    return frame_valid;													   //static volatile uint8_t frame_valid = 1;
}

/* ════════════════════════════════════════════════════════════
 * modbus_transport_frame_clear — 清"帧完成"标志
 *
 * 原型：void modbus_transport_frame_clear(void)
 * 返值：无
 *
 * 调用示例：
 *   modbus_transport_frame_clear();  // 处理完一帧后调
 * ════════════════════════════════════════════════════════════ */

void modbus_transport_frame_clear(void)
{
    frame_ready = 0;                        /* 清除帧完成标志，允许进入下一帧接收 */
    frame_valid = 1;                        /* 重置帧有效性为默认有效状态 */
    silence_ms = 0;                         /* 清零静默计时，防止误判 */
}


/* ════════════════════════════════════════════════════════════
 * modbus_transport_on_timer — T3.5 定时器溢出时调用
 *
 * 原型：uint8_t modbus_transport_on_timer(void)
 * 返值：1 = 本次节拍刚达到 T3.5，0 = 尚未完成新帧
 *
 * 原理：
 *   TIM4 溢出 = 静默超过 T3.5 = 一帧结束。
 *   由 HAL_TIM_PeriodElapsedCallback（在 TIM6.c，所有 TIM 共用）里的
 *   TIM4 分支调用，避免重复定义回调。
 *
 * 调用示例：
 *   if (modbus_transport_on_timer()) { notify_modbus_task(); }
 * ════════════════════════════════════════════════════════════ */
uint8_t modbus_transport_on_timer(void)
{
    if (bsp_485_rx_len == 0U || frame_ready != 0U)
        return 0U;                         /* 无接收数据或已有待处理帧，避免重复通知任务 */

    if (silence_ms < 255U)
        silence_ms++;                       /* 静默时间 +1ms（上限 255 防止溢出回卷） */
    if (silence_ms >= 5U)
    {
        frame_ready = 1;                    /* 持续静默达到 T3.5：正常判定一帧接收结束 */
        return 1U;                          /* 通知上层：本次刚产生一个完整帧事件 */
    }

    return 0U;
}
