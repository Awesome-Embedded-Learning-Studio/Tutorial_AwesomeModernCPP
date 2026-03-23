extern "C" {
#include "stm32f1xx_hal.h"

void SystemClock_Config(void);
}

#include "led/led.hpp"

// 使用模板LED类 - 编译期指定端口和引脚
// 板载LED在PC13（低电平点亮）
hal::Led<GPIOC_BASE, GPIO_PIN_13> led; // 方式1：直接指定地址
// hal::LedPC13 led;                     // 方式2：使用类型别名

int main(void) {
    HAL_Init();           // 初始化 HAL，配置 SysTick 为 1ms 中断
    SystemClock_Config(); // 配置系统时钟
    led.init();           // 配置 LED 引脚

    while (1) {
        led.toggle();   // 模板方法切换LED状态
        HAL_Delay(500); // 500ms
    }
}
