---
id: "112"
title: "在线代码运行和汇编查看（Compiler Explorer 集成）"
category: interactive
priority: P3
status: done
created: 2026-04-15
assignee: charliechen
depends_on: ["081"]
blocks: []
estimated_effort: large
---

# 在线代码运行和汇编查看（Compiler Explorer 集成）

## 目标

在 VitePress 站点中集成轻量级 Compiler Explorer (Godbolt) 组件，让读者可以在教程页面中按需运行 C++ demo 或查看优化后的汇编输出。

第一版聚焦嵌入式零开销主题：

- 带 `main()` 且适合宿主机执行的 demo 提供“运行”按钮。
- 嵌入式/裸机相关 demo 提供 x86-64 与 ARM Cortex-M 汇编查看。
- 不默认嵌入 Godbolt iframe；只有读者点击按钮时才读取源码并请求 Compiler Explorer API。
- “打开 Godbolt”按钮使用 `/clientstate/` 打开完整编辑环境，作为高级交互入口。

## 验收标准

- [x] 采用 VitePress 组件方案，不再使用旧 MkDocs `documents/javascripts/compiler-explorer.js` 方案
- [x] 新增 `site/.vitepress/theme/components/OnlineCompilerDemo.vue`
- [x] 组件已注册到 `site/.vitepress/theme/index.ts`
- [x] 支持“运行”“看 x86-64 汇编”“看 ARM 汇编”三类动作
- [x] 支持站内编辑源码后重新运行/查看汇编
- [x] 编译失败时展示 Compiler Explorer 返回的 stderr/build diagnostics
- [x] 支持 ARM Cortex-M 目标汇编输出查看
- [x] 组件只在用户交互后请求源码和 Godbolt API，不影响首屏加载
- [x] `site/.vitepress/theme/custom.css` 包含亮色/暗色和移动端样式
- [x] 至少 3 篇教程嵌入了在线 demo 组件
- [x] 对不适合宿主机执行的 MMIO 示例，只开放汇编查看
- [x] 提供 Godbolt 完整外链作为备选交互入口

## 实施概要

### VitePress 组件

核心组件：`site/.vitepress/theme/components/OnlineCompilerDemo.vue`

组件属性示例：

```md
<OnlineCompilerDemo
  title="constexpr 波特率分频：运行结果与优化输出"
  source-path="code/examples/chapter02/01_zero_overhead/constexpr_example.cpp"
  arm-source-path="code/examples/compiler_explorer/constexpr_baud_arm.cpp"
  description="这个 demo 可以在宿主机运行，也可以对比 x86-64 与 Cortex-M 的优化输出。"
  allow-run
  allow-x86-asm
  allow-arm-asm
/>
```

默认编译器：

| 动作 | Compiler Explorer ID | 默认参数 |
| ---- | -------------------- | -------- |
| 运行 | `g152` | `-O2 -std=c++20` |
| x86-64 汇编 | `g152` | `-O2 -std=c++20` |
| ARM 汇编 | `armg1520` | `-O2 -std=c++20 -mcpu=cortex-m3 -mthumb -ffreestanding -fno-exceptions -fno-rtti` |

兼容性需要时可以覆盖为 `armg1320`。

### 数据流

1. 页面初始渲染只显示组件外壳、源码路径和按钮。
2. 读者点击按钮后，组件从 GitHub raw URL 读取 `source-path` 或 `arm-source-path` 指向的源码。
3. 组件调用 `https://godbolt.org/api/compiler/{compilerId}/compile`。
4. 读者可以展开源码编辑区，修改后再次运行或查看汇编。
5. 运行模式显示 stdout/stderr；汇编模式显示 asm；编译失败时显示 stderr/build diagnostics，而不是只显示 `<Compilation failed>`。
6. 点击“打开 Godbolt”时，将当前源码、编译器和参数编码到 `/clientstate/` URL 并打开完整 Godbolt 页面。

### 首批嵌入点

- `documents/vol8-domains/embedded/01-zero-overhead-abstraction.md`
  - GPIO 位操作：C 宏 vs C++ 类型安全抽象
  - constexpr 波特率分频：运行结果与优化输出
- `documents/vol8-domains/embedded/04-crtp-vs-runtime-polymorphism.md`
  - 编译期多态：模板 `poll` 的内联机会
- `documents/vol8-domains/embedded/05-fixed-pool-allocation.md`
  - 固定池分配器：O(1) 分配与释放 demo

## 测试记录

- [x] `pnpm run build`
- [x] 通过 Compiler Explorer API 验证 `g152` executor 请求能返回 `stdout`
- [x] 通过 Compiler Explorer API 验证 `armg1520` + Cortex-M 参数能返回 ARM 汇编
- [x] VitePress 构建验证源码编辑器与编译诊断展示逻辑

## 后续

后续推全工作转入 `todo/interactive/114-online-compiler-rollout-guide.md`。

## 参考资料

- [Compiler Explorer API](https://github.com/compiler-explorer/compiler-explorer/blob/main/docs/API.md)
- [Godbolt Client State](https://godbolt.org/clientstate.html)
