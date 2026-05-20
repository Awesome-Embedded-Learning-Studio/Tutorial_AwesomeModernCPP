---
id: 036
title: "嵌入式 DSP/信号处理入门"
category: content
priority: P3
status: pending
created: 2026-04-15
assignee: charliechen
depends_on: ["architecture/002"]
blocks: []
estimated_effort: large
---

# 嵌入式 DSP/信号处理入门

## 目标
编写嵌入式数字信号处理（DSP）入门教程。覆盖采样定理、FIR/IIR 滤波器的 C++ 实现、定点数运算、FFT 基础，以及 ARM CMSIS-DSP 库的使用。使用 STM32F1 的 ADC 采集真实信号作为演示数据源，将理论知识与实践紧密结合。

## 验收标准
- [ ] 采样定理和信号基础概念文档
- [ ] FIR 滤波器 C++ 模板类实现
- [ ] IIR 滤波器（双二阶/Biquad）C++ 实现
- [ ] 定点数运算库（Q15/Q31 格式）
- [ ] FFT 基础概念和 radix-2 实现
- [ ] CMSIS-DSP 库集成和使用教程
- [ ] 使用 STM32F1 ADC 采集真实信号的完整示例
- [ ] 滤波器设计工具使用指南（Python scipy.signal）
- [ ] 频谱分析示例
- [ ] 浮点 vs 定点性能对比数据

## 实施说明
DSP 在嵌入式传感器数据处理、音频处理、通信系统中广泛应用。本教程面向有 C++ 基础但无信号处理背景的嵌入式开发者。

**内容结构规划：**

1. **信号处理基础** — 连续信号与离散信号。采样定理（Nyquist 定理）：采样频率必须大于信号最高频率的 2 倍。混叠现象和抗混叠滤波器。量化误差和信噪比（SNR）。时域和频域：傅里叶变换的直觉理解。使用 Python + matplotlib 可视化信号（辅助工具）。

2. **FIR 滤波器** — FIR 滤波器原理：有限脉冲响应、线性相位特性。卷积运算：y[n] = sum(h[k] * x[n-k])。C++ 模板实现：`FirFilter<N>` 类，编译期指定阶数。系数设计：窗函数法（Hamming/Kaiser）、使用 scipy.signal.firwin 生成系数。示例：低通滤波器去除高频噪声。STM32F1 + ADC 采集 + FIR 滤波的完整示例。性能分析：浮点 vs 定点运算。

3. **IIR 滤波器** — IIR 滤波器原理：无限脉冲响应、反馈结构、非线性相位。双二阶（Biquad）结构：二阶 IIR 的标准形式。Direct Form I 和 Direct Form II 实现。C++ 实现：`BiquadFilter` 类，`IirCascade<N>` 级联实现。系数设计：使用 scipy.signal.iirfilter 生成系数。稳定性分析：极点位置。示例：带通滤波器提取特定频率分量。

4. **定点数运算** — 为什么需要定点数：Cortex-M3 无硬件浮点（STM32F103）。Q 格式：Q15（16位）、Q31（32位）的定义和范围。定点算术：乘法（需舍入和饱和）、加法（需溢出检测）。C++ Q-number 类：`Q15` 和 `Q31` 类型，运算符重载。定点 FIR/IIR 实现。浮点与定点的精度对比。

5. **FFT 基础** — DFT（离散傅里叶变换）定义和直觉理解。FFT 算法：Cooley-Tukey radix-2 算法。C++ 实现：简化的 radix-2 FFT。窗函数：为什么 FFT 前需要加窗（Hann/Hamming）。频谱分析：从 FFT 输出计算幅度谱和功率谱。示例：ADC 采集信号的频谱分析。频率分辨率与采样点数的关系。

6. **CMSIS-DSP 库** — CMSIS-DSP 概述：ARM 官方优化 DSP 库。在 STM32F103 上集成 CMSIS-DSP。常用函数：arm_fir_f32/arm_fir_q31、arm_biquad_cascade_df1_f32、arm_cfft_f32。CMSIS-DSP 与自实现代码的性能对比。CMSIS-DSP 的编译器优化（利用 Cortex-M3 的 DSP 指令如 SMMLAR）。

7. **综合项目** — 实时音频频谱分析仪：ADC 采集音频信号 -> FFT 频谱分析 -> 获取各频段幅度。或者：环境噪声监测仪：MIC 采集 -> 带通滤波 -> A加权 -> 分贝计算。使用 STM32F1 ADC + DMA 采集 + CMSIS-DSP 处理。

## 涉及文件
- documents/embedded/topics/dsp-basics/index.md
- documents/embedded/topics/dsp-basics/01-signal-fundamentals.md
- documents/embedded/topics/dsp-basics/02-fir-filter.md
- documents/embedded/topics/dsp-basics/03-iir-filter.md
- documents/embedded/topics/dsp-basics/04-fixed-point.md
- documents/embedded/topics/dsp-basics/05-fft-basics.md
- documents/embedded/topics/dsp-basics/06-cmsis-dsp.md
- documents/embedded/topics/dsp-basics/07-project.md
- codes/embedded/dsp/ (配套代码和滤波器系数)

## 参考资料
- 《Understanding Digital Signal Processing》(3rd Ed) — Richard G. Lyons
- 《DSP for Embedded and Real-Time Systems》— Robert Oshana
- ARM CMSIS-DSP 文档 (keil.com/pack/doc/CMSIS/DSP)
- scipy.signal 文档 (Python 滤波器设计)
- 《The Scientist and Engineer's Guide to Digital Signal Processing》— Steven W. Smith (免费在线)
- Joseph Yiu 的 Cortex-M3/M4 DSP 指令说明
