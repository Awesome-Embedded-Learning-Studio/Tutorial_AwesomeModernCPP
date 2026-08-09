# STM32F407 教程代码(重建版)

现代 C++ 嵌入式主线的代码工程,目标芯片 **STM32F407ZGT6**(Cortex-M4F)。
**以 Renode 模拟器为开发主线**,真板为可选后半程——不用买板子也能写、跑、验证。

老 F1 工程(`code/stm32f1-tutorials/`)留作参考,不删。

## 结构

```
toolchain-arm-none-eabi.cmake   各工程共用的交叉编译工具链(独立文件,不内联)
renode/                         模拟器主线(跑在 Renode 里的工程;真板工程以后另放)
  0_blink/                      第 0 个工程:最小闪灯(裸机寄存器,翻转 PD12)
    CMakeLists.txt
    src/main.c
    platform/startup.s          Cortex-M4F 启动(向量表 + 复位处理)
    platform/stm32f407zg.ld     F407ZGT6 链接脚本
    blink.resc                  Renode 启动脚本(.resc 平铺在工程根)
```

后续工程会按 `1_button/` `2_uart/` ... 增长,每条外设走"C → 类型安全寄存器 → 模板 → C++23"的重构阶梯,每步都在 Renode 验证。

## 工具链前提(本机已就绪)

- `arm-none-eabi-gcc` 16.1(交叉编译,M4F + FPU)
- `cmake` ≥ 3.16
- `renode` 1.16(模拟器,本机 `/usr/sbin/renode`)
- `openocd` 0.12(真板烧录,等核心板到位用)

## 怎么跑第一个工程

```bash
cd code/stm32f4-tutorials/renode/0_blink
cmake -B build -DCMAKE_TOOLCHAIN_FILE=../../toolchain-arm-none-eabi.cmake
cmake --build build
cmake --build build --target run_blink_in_renode       # 第 1 档(C 裸机基线)进 Renode 跑
cmake --build build --target run_blink_mmio_in_renode  # 第 2 档(mmio 封装)进 Renode 跑
```

`run_in_renode` 会跑 1 秒虚拟时间,然后连采几次 GPIOD_ODR,看到 `0x00001000` 和 `0x00000000` 交替就是 PD12 在闪。

## 三目标构建(规划中)

| 目标 | 状态 | 用途 |
|---|---|---|
| `blink.elf` + hex/bin | ✅ | 交叉编译产物 |
| `run_in_renode` | ✅ | 模拟器里跑(主开发回路) |
| `flash`(openocd) | 待板子 | 烧到真 F407 核心板 |
| host 单测(Catch2/doctest) | 待可分离逻辑 | 纯 C++ 逻辑层(消抖状态机/环形缓冲)在 host 验证、进 CI |
