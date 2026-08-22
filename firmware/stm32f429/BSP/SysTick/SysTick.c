#include "./SysTick/SysTick.h"

/* ── 系统时钟配置：HSE(25M) → PLL → 180MHz ── */
static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState       = RCC_HSE_ON;
    osc.PLL.PLLState   = RCC_PLL_ON;
    osc.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM       = 25;
    osc.PLL.PLLN       = 360;
    osc.PLL.PLLP       = RCC_PLLP_DIV2;
    osc.PLL.PLLQ       = 7;
    HAL_RCC_OscConfig(&osc);

    clk.ClockType      = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK
                       | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV4;
    clk.APB2CLKDivider = RCC_HCLK_DIV2;
    HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_5);
}

/**
 * @brief  SysTick 初始化 — 时钟 180MHz + 轮询延时初始化
 */
void SysTick_Init(void)
{
    SystemClock_Config();
    delay_init(SystemCoreClock);
}

/* ════════════════════════════════════════════════════════════
 *  轮询版延时 — 读 SysTick->VAL 硬件寄存器，不依赖中断
 *
 *  SysTick 每个 HCLK 周期 VAL 自动减 1。
 *  fac_us = SystemCoreClock / 1,000,000（180MHz → 180 tick/μs）
 *  delay_us(999) < 1ms 绕开计数器周期边界死循环。
 * ════════════════════════════════════════════════════════════ */

static uint32_t fac_us;

void delay_init(uint32_t sysclk)
{
    fac_us = sysclk / 1000000;
}

void delay_us(uint32_t nus)
{
    uint32_t target = nus * fac_us;
    uint32_t start  = SysTick->VAL;

    while (1)
    {
        uint32_t now = SysTick->VAL;
        uint32_t elapsed;
        if (now <= start)
            elapsed = start - now;
        else
            elapsed = (SysTick->LOAD - now) + start + 1;
        if (elapsed >= target) break;
    }
}

void delay_ms(uint32_t nms)
{
    while (nms--)
        delay_us(999);
}
