#include "stm32f1xx_hal.h"

/* ────────────────────────────────────────────────────────────
 * Blue Pill 板载 LED
 *   - 接在 PC13
 *   - 低电平点亮，高电平熄灭
 * ──────────────────────────────────────────────────────────── */
#define LED_PORT GPIOC
#define LED_PIN GPIO_PIN_13

/* ── 函数声明 ─────────────────────────────────────────────── */
static void SystemClock_Config(void);
static void GPIO_Init(void);

/* ── main ────────────────────────────────────────────────── */
int main(void) {
  HAL_Init();           /* 初始化 HAL，配置 SysTick 为 1ms 中断 */
  SystemClock_Config(); /* 配置系统时钟                          */
  GPIO_Init();          /* 配置 LED 引脚                         */

  while (1) {
    HAL_GPIO_TogglePin(LED_PORT, LED_PIN);
    HAL_Delay(500); /* 500ms，依赖 SysTick                   */
  }
}

/* ── 时钟配置：HSI → PLL → 64MHz ────────────────────────────
 *
 *  HSI = 8MHz
 *  PLLSRC = HSI / 2 = 4MHz
 *  PLLMUL = x16 → SYSCLK = 64MHz   （HSI 来源最高只能到 64MHz）
 *
 *  APB1 (低速总线) 最高 36MHz → 2 分频 = 32MHz
 *  APB2 (高速总线)             1 分频 = 64MHz
 * ──────────────────────────────────────────────────────────── */
static void SystemClock_Config(void) {
  RCC_OscInitTypeDef osc = {0};
  osc.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  osc.HSIState = RCC_HSI_ON;
  osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  osc.PLL.PLLState = RCC_PLL_ON;
  osc.PLL.PLLSource = RCC_PLLSOURCE_HSI_DIV2; /* 4MHz  */
  osc.PLL.PLLMUL = RCC_PLL_MUL16;             /* 64MHz */
  HAL_RCC_OscConfig(&osc);

  RCC_ClkInitTypeDef clk = {0};
  clk.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
                  RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  clk.AHBCLKDivider = RCC_SYSCLK_DIV1; /* HCLK  = 64MHz */
  clk.APB1CLKDivider = RCC_HCLK_DIV2;  /* APB1  = 32MHz */
  clk.APB2CLKDivider = RCC_HCLK_DIV1;  /* APB2  = 64MHz */
  HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2);
}

/* ── GPIO 初始化 ─────────────────────────────────────────── */
static void GPIO_Init(void) {
  __HAL_RCC_GPIOC_CLK_ENABLE(); /* 使能 GPIOC 时钟，必须先做 */

  GPIO_InitTypeDef cfg = {0};
  cfg.Pin = LED_PIN;
  cfg.Mode = GPIO_MODE_OUTPUT_PP;  /* 推挽输出 */
  cfg.Speed = GPIO_SPEED_FREQ_LOW; /* LED 不需要高速 */
  HAL_GPIO_Init(LED_PORT, &cfg);

  /* 初始状态：高电平（LED 灭）*/
  HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_SET);
}

/* ── SysTick 中断处理（HAL_Delay 依赖此函数）────────────── */
void SysTick_Handler(void) { HAL_IncTick(); }
