#include "./IWDG/iwdg.h"

IWDG_HandleTypeDef IWDG_Handle;                                      // IWDG 句柄

/* ════════════════════════════════════════════════════════════
 * IWDG_Config — 配置独立看门狗超时时间并启动
 *
 * 原型：void IWDG_Config(uint8_t prv, uint16_t rlv)
 *       prv — [输入] 预分频系数，取值：
 *             IWDG_PRESCALER_4 / 8 / 16 / 32 / 64 / 128 / 256
 *       rlv — [输入] 重装载值，范围 0~0xFFF（4095）
 * 返值：无
 *
 * 原理：独立看门狗由 LSI（~40KHz）驱动，不依赖系统时钟。
 *       就算 PLL 崩了、HSE 停了，IWDG 照样跑。
 *       超时公式：Tout ≈ (Reload + 1) × Prescaler / LSI （秒）。
 *       LSI 典型频率约 40kHz，实际值会随芯片和温度变化。
 *       一旦启动就无法停止，只能喂狗推迟复位。
 *
 * 调用示例：
 *   IWDG_Config(IWDG_PRESCALER_256, 625);  // (625+1)×25/40000 ≈ 4 秒超时
 * ════════════════════════════════════════════════════════════ */
void IWDG_Config(uint8_t prv, uint16_t rlv)
{
    IWDG_Handle.Instance = IWDG;                                     // IWDG 外设
    IWDG_Handle.Init.Prescaler = prv;                                 // 将 LSI 时钟40MHZ进行 256 分频
    IWDG_Handle.Init.Reload    = rlv;                                 // 625U，看门狗递减计数器的重装值
    HAL_IWDG_Init(&IWDG_Handle);                                      // 初始化 IWDG
    __HAL_IWDG_START(&IWDG_Handle);                                   // 启动看门狗，开始倒计时
}

/* ════════════════════════════════════════════════════════════
 * IWDG_Feed — 喂狗（重置递减计数器）
 *
 * 原型：void IWDG_Feed(void)
 *       无参数，无返回值
 *
 * 原理：把 Reload 值重新装进递减计数器，倒计时从头开始。
 *       在超时之前喂狗，系统就不会复位。
 *
 * 调用示例：
 *   IWDG_Feed();  // KEY 按下时调一次，狗吃饱了
 * ════════════════════════════════════════════════════════════ */
void IWDG_Feed(void)
{
    HAL_IWDG_Refresh(&IWDG_Handle);                                   // 重装计数器 = 喂狗
}
