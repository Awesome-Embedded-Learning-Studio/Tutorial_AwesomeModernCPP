# STM32 教程代码(现代 C++ 主线)

嵌入式线"两幕一桥"的代码工程。中心是现代 C++ 在 STM32 上的开发方式,芯片是道具:
第一幕从门槛最低的 STM32F103(Blue Pill,十块钱)起步,第二幕走到资源宽裕的 STM32F407。

每一站都跑在 Renode 模拟器里,不用买板子也能写、跑、验证;真板是每站末尾的可选项。

## 结构

```
toolchain-arm-none-eabi.cmake   各工程共用的交叉编译工具链(独立文件,不内联)
f103/                           第一幕:STM32F103 + Renode
  0_blink/                      第 0 个工程:最小闪灯(裸机寄存器,翻转 PC13)
    CMakeLists.txt
    src/main.c                  第 1 档:裸机寄存器(C,基线)
    src/main_mmio.cpp           第 2 档:mmio_reg 类型安全封装
    src/main_fields.cpp         第 3 档:位域 + enum class
    src/main_gpio_template.cpp  第 4 档:模板 GPIO/LED
    src/main_cpp23.cpp          第 5 档:C++23 consteval/constinit/属性
    src/main_uart.cpp           加餐:闪灯 + printf 走 UART1(配合 showAnalyzer)
    platform/startup.s          Cortex-M3 启动(向量表 + 复位处理)
    platform/stm32f103c8.ld     F103C8T6 链接脚本(64K Flash / 20K SRAM)
    platform/blue_pill.repl     Blue Pill 板级描述(Renode 平台文件,自维护)
    blink*.resc                 Renode 启动脚本(.resc 平铺在工程根)
```

后续按站增长(`1_button/` `2_uart/` ...),每条外设走"C → 类型安全寄存器 → 模板 → C++23"
的重构阶梯,每档都在 Renode 验证。

## 工具链前提(本机已就绪)

- `arm-none-eabi-gcc`(交叉编译,cortex-m3)
- `cmake` ≥ 3.16
- `renode`(模拟器,本机 `/usr/sbin/renode`)
- `openocd`(真板烧录,等 Blue Pill 到位用)

## 怎么跑第一个工程

```bash
cd code/stm32-tutorials/f103/0_blink
cmake -B build -DCMAKE_TOOLCHAIN_FILE=../../toolchain-arm-none-eabi.cmake
cmake --build build
cmake --build build --target run_blink_in_renode       # 第 1 档(C 裸机基线)进 Renode 跑
cmake --build build --target run_blink_mmio_in_renode  # 第 2 档(mmio 封装)进 Renode 跑
```

`run_in_renode` 会跑 1 秒虚拟时间,然后连采几次 GPIOC_ODR,看到 `0x00002000` 和
`0x00000000` 交替就是 PC13 在闪(Blue Pill 的板载 LED 低电平点亮)。

## 构建目标

| 目标 | 状态 | 用途 |
|---|---|---|
| `blink.elf` + hex/bin | ✅ | 交叉编译产物 |
| `run_in_renode` | ✅ | 模拟器里跑(主开发回路) |
| `flash`(openocd) | 待板子 | 烧到真 Blue Pill |
| host 单测(Catch2/doctest) | 待可分离逻辑 | 纯 C++ 逻辑层(消抖状态机/环形缓冲)在 host 验证、进 CI |

## Renode 平台说明

Renode 稳定版(1.16.x)没有现成的 Blue Pill 板级文件,`platform/blue_pill.repl` 是本教程
自己维护的板级描述:基于官方 `platforms/cpus/stm32f103.repl` 挂上板载 LED(PC13)。
后续按键站会在这里补 Button 外设。
