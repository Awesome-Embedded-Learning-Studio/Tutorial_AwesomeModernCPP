#include "stm32f1xx_hal.h"

void SystemClock_Config(void) {
    RCC_OscInitTypeDef osc = {0};
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    osc.HSIState = RCC_HSI_ON;
    osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSI_DIV2; /* 4MHz  */
    osc.PLL.PLLMUL = RCC_PLL_MUL16;             /* 64MHz */
    HAL_RCC_OscConfig(&osc);

    RCC_ClkInitTypeDef clk = {0};
    clk.ClockType =
        RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1; /* HCLK  = 64MHz */
    clk.APB1CLKDivider = RCC_HCLK_DIV2;  /* APB1  = 32MHz */
    clk.APB2CLKDivider = RCC_HCLK_DIV1;  /* APB2  = 64MHz */
    HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2);
}

void SysTick_Handler(void) {
    HAL_IncTick();
}
