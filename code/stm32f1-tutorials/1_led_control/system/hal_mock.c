#include "stm32f1xx_hal.h"

void SysTick_Handler(void) {
    HAL_IncTick();
}
