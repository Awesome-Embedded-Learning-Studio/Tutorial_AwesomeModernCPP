#include "stm32f1xx_hal.h"

void SysTick_Handler(void) {
    HAL_IncTick();
}

/* EXTI interrupt handlers for button on PA0 (EXTI0) */
void EXTI0_IRQHandler(void) {
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_0);
}

/* Weak callback override — called by HAL_GPIO_EXTI_IRQHandler */
__attribute__((weak)) void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
    (void)GPIO_Pin;
}
