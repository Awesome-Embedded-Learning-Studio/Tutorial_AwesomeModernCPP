---
id: "114"
title: "在线编译器组件推全指南"
category: interactive
priority: P3
status: active
created: 2026-05-20
assignee: charliechen
depends_on: ["112"]
blocks: []
estimated_effort: medium
---

# 在线编译器组件推全指南

这份指南用于把 `OnlineCompilerDemo` 推广到更多教程页面。目标是让读者能在页面内编辑源码、运行 host demo、查看 x86-64/ARM 汇编，并在失败时看到具体编译诊断。

## 什么时候适合插入

优先插入这些位置：

- 讲“零开销抽象”“constexpr”“模板/CRTP”“位操作”“内存池”“无锁/原子”等需要看优化结果的段落。
- 代码已有 `main()`，且适合在 Compiler Explorer 的 x86-64 executor 上运行。
- 嵌入式代码能整理出一份 freestanding ARM 精简源码，只依赖 `<cstdint>`、`<cstddef>`、`<type_traits>` 等裸机友好头文件。

暂时不要插入这些位置：

- 代码依赖本地多文件工程、第三方库、板级 HAL、RTOS 或外设头文件，且还没有可独立编译的精简版。
- 示例需要真实硬件运行，页面内运行会误导读者。
- 示例过长，读者很难在页面内编辑和理解输出。

## 组件用法

非 ARM：host 可运行 + x86 汇编：

```md
<OnlineCompilerDemo
  title="std::function vs 函数指针"
  source-path="code/examples/chapter09/03_std_function_vs_pointers/function_pointer.cpp"
  description="这个 demo 在 x86-64 上运行，并查看优化后的汇编。"
  allow-run
  allow-x86-asm
/>
```

非 ARM：只看 x86 汇编：

```md
<OnlineCompilerDemo
  title="RVO / NRVO 优化观察"
  source-path="code/examples/chapter03/03_rvo_nrvo/rvo_nrvo_example.cpp"
  description="这个 demo 只用于查看优化后的 x86-64 汇编。"
  allow-x86-asm
/>
```

host 可运行 + x86 汇编：

```md
<OnlineCompilerDemo
  title="constexpr 波特率分频：运行结果与优化输出"
  source-path="code/examples/chapter02/01_zero_overhead/constexpr_example.cpp"
  description="这个 demo 可以在宿主机运行，也可以查看优化后的 x86-64 汇编。"
  allow-run
  allow-x86-asm
/>
```

host 可运行 + ARM 精简源码：

```md
<OnlineCompilerDemo
  title="编译期多态：模板 poll 的内联机会"
  source-path="code/examples/chapter02/04_crtp_polymorphism/compile_time_polymorphism.cpp"
  arm-source-path="code/examples/compiler_explorer/static_polymorphism_arm.cpp"
  description="运行按钮使用完整 host demo；ARM 汇编按钮使用 freestanding 精简源码。"
  allow-run
  allow-x86-asm
  allow-arm-asm
/>
```

只看 ARM 汇编：

```md
<OnlineCompilerDemo
  title="GPIO 位操作：C 宏 vs C++ 类型安全抽象"
  source-path="code/examples/chapter02/01_zero_overhead/gpio_example.cpp"
  arm-source-path="code/examples/compiler_explorer/gpio_zero_overhead_arm.cpp"
  description="这个示例包含真实 MMIO 地址，不在宿主机执行，只观察汇编。"
  allow-arm-asm
/>
```

## 源码准备规则

- `source-path` 指向原始教程 demo，优先复用 `code/examples/...` 中已有文件。
- 非 ARM 示例不需要 `arm-source-path`，只声明 `allow-run`、`allow-x86-asm` 或二者之一即可。
- `arm-source-path` 只在声明 `allow-arm-asm` 时需要，指向 `code/examples/compiler_explorer/*_arm.cpp`，只保留要观察的核心函数。
- ARM 精简源码避免 `iostream`、`string`、容器、异常、动态分配和 hosted-only 标准库头。
- MMIO 示例不要开放 `allow-run`，避免读者在 host executor 上运行真实寄存器地址访问。
- 如果新增 `arm-source-path`，也把同一份源码加入组件内置 fallback，避免文档发布后 raw GitHub 文件尚未同步时按钮失败。

## 推荐命名

- `*_arm.cpp`：只服务 ARM Cortex-M 汇编查看。
- 文件名用主题命名，例如 `constexpr_baud_arm.cpp`、`fixed_pool_arm.cpp`。
- 每个文件应能单独被 `armg1520` 编译，不依赖相对 include 或 CMake 工程。

## 测试清单

每次新增页面嵌入后至少检查以下项目。优先按“静态检查 -> 构建检查 -> API 检查 -> 浏览器手测”的顺序执行，这样失败时定位最快。

### 1. 静态检查

确认新增的组件引用、源码文件和 ARM 精简源码都存在：

```bash
rg -n "<OnlineCompilerDemo" documents
rg -n "arm-source-path=\"code/examples/compiler_explorer/" documents
find code/examples/compiler_explorer -maxdepth 1 -type f | sort
```

预期结果：

- 每个新增组件都能在 `rg "<OnlineCompilerDemo"` 输出中看到。
- 如果组件声明了 `allow-arm-asm`，优先同时声明 `arm-source-path`。
- 如果组件没有声明 `allow-arm-asm`，不需要 `arm-source-path`，页面也不应出现“看 ARM 汇编”或“编辑 ARM 源码”按钮。
- `arm-source-path` 指向的文件真实存在于 `code/examples/compiler_explorer/`。

检查 MMIO 示例是否误开 host 运行：

```bash
rg -n "0x400[0-9A-Fa-f]+|reinterpret_cast<volatile|volatile std::uint32_t\\*" code/examples documents
```

预期结果：

- 真实寄存器地址或 volatile MMIO 示例不应暴露 `allow-run`。
- 这类示例只开放 `allow-arm-asm`，或只开放汇编查看。

检查 ARM 精简源码是否混入 hosted-only 依赖：

```bash
rg -n "#include <(iostream|string|vector|map|unordered_map|memory|thread|mutex|future|filesystem|cassert|new)>" code/examples/compiler_explorer
rg -n "std::cout|std::string|std::vector|new |delete |throw |try |catch" code/examples/compiler_explorer
```

预期结果：

- 上述命令最好没有输出。
- 如果有输出，需要确认它是否真的能在 `armg1520 -ffreestanding` 下编译。

### 2. 构建检查

运行站点构建：

```bash
pnpm run build
```

预期结果：

- 构建最终显示 `Status: ✓ SUCCESS`。
- 如果在受限沙箱中出现 `/tmp/tsx-*/... EPERM`，这是 `tsx` IPC pipe 权限问题，不是站点代码问题；在普通本地 shell 中重跑同一命令。

确认构建产物包含组件：

```bash
rg -n "online-compiler-demo|Compiler Explorer|编辑源码|看 ARM 汇编" site/.vitepress/dist | head -80
```

预期结果：

- 输出中能看到 `online-compiler-demo` 或对应组件文案。

### 3. Compiler Explorer API 检查

对每个新增 ARM 精简源码，直接调用 Compiler Explorer API。把 `<file>` 换成实际文件：

```bash
jq -n --rawfile source <file> '{
  source: $source,
  options: {
    userArguments: "-O2 -std=c++20 -mcpu=cortex-m3 -mthumb -ffreestanding -fno-exceptions -fno-rtti",
    compilerOptions: { executorRequest: false },
    filters: {
      binary: false,
      commentOnly: true,
      demangle: true,
      directives: true,
      labels: true,
      libraryCode: false,
      trim: false
    }
  }
}' |
curl -sS \
  -H 'Accept: application/json' \
  -H 'Content-Type: application/json' \
  --data-binary @- \
  https://godbolt.org/api/compiler/armg1520/compile |
jq -r 'if ((.asm // []) | length) > 0
  then (.asm[0:12] | map(.text) | join("\n"))
  else ((.stderr // .buildResult.stderr // []) | map(.text) | join("\n"))
  end'
```

预期结果：

- 成功时输出函数标签和 ARM 指令，例如 `bx lr`、`ldr`、`add`、`udiv` 等。
- 不应只输出 `<Compilation failed>`。
- 如果失败，应能看到具体 stderr；按“常见失败与处理”修复。

对 host 可运行 demo，直接验证 executor。把 `<file>` 换成 `source-path` 指向的文件：

```bash
jq -n --rawfile source <file> '{
  source: $source,
  options: {
    userArguments: "-O2 -std=c++20",
    compilerOptions: { executorRequest: true },
    filters: { execute: true },
    executeParameters: { args: "", stdin: "" }
  }
}' |
curl -sS \
  -H 'Accept: application/json' \
  -H 'Content-Type: application/json' \
  --data-binary @- \
  https://godbolt.org/api/compiler/g152/compile |
jq '{code, didExecute, stdout, stderr, buildCode: .buildResult.code}'
```

预期结果：

- `didExecute` 为 `true`。
- `code` 为 `0`。
- `stdout` 中有示例输出；如果 demo 本来不输出，也要确认没有崩溃。

### 4. 浏览器手测

启动本地站点：

```bash
pnpm run dev
```

打开包含组件的页面，例如：

- `http://localhost:5173/Tutorial_AwesomeModernCPP/vol8-domains/embedded/01-zero-overhead-abstraction`
- `http://localhost:5173/Tutorial_AwesomeModernCPP/vol8-domains/embedded/04-crtp-vs-runtime-polymorphism`
- `http://localhost:5173/Tutorial_AwesomeModernCPP/vol8-domains/embedded/05-fixed-pool-allocation`

逐个组件检查：

- 初始页面不应加载 iframe，也不应自动显示运行结果。
- 点击“运行”：结果区显示 stdout/stderr，按钮在请求期间显示“处理中...”。
- 点击“看 x86-64 汇编”：结果区显示 x86-64 汇编或清晰诊断。
- 点击“看 ARM 汇编”：结果区显示 ARM 汇编，不出现只有 `<Compilation failed>` 的空失败。
- 点击“编辑源码”：出现可编辑 textarea；修改返回值或输出文本后，再点击“运行/汇编”，结果应反映修改。
- 点击“编辑 ARM 源码”：修改 ARM 精简源码后，“看 ARM 汇编”应使用编辑后的源码。
- 故意制造错误，例如把函数体改成 `return ;` 或删除分号；再次编译应显示具体错误行和诊断。
- 点击“还原源码”：textarea 回到文件原始内容或内置 fallback 内容。
- 点击“打开 Godbolt”：新页面应打开 `godbolt.org/clientstate/...`，并带上当前编辑源码。

### 5. 移动端/布局检查

浏览器打开开发者工具，至少检查一个窄屏尺寸：

- 390x844（常见手机竖屏）
- 768x1024（平板）

预期结果：

- 按钮自动换行，不挤出正文。
- `source-path` 长路径会换行，不遮挡标题。
- 源码编辑区和汇编输出区可以滚动，页面整体不出现明显横向溢出。
- 错误信息块不会盖住后续正文。

### 6. 提交前核对

```bash
git status --short
git diff --stat
rg -n "<OnlineCompilerDemo" documents
```

预期结果：

- 新增的 `code/examples/compiler_explorer/*.cpp` 和 `README.md` 应纳入提交。
- 新增或修改的文档页应只包含相关组件和必要上下文，不混入无关重写。
- 如果新增了 `arm-source-path`，确认对应源码文件和组件 fallback 都同步更新。

## 常见失败与处理

- `This header is not available in freestanding mode.`：ARM 精简源码包含了 hosted-only 标准库头，移除 `iostream`、`string`、容器等依赖。
- `<Compilation failed>` 但没有诊断：检查组件是否仍优先展示 `asm`，应改为编译失败时显示 stderr/build diagnostics。
- raw GitHub 404：新增源码尚未提交或尚未推到默认分支；提交 `code/examples/compiler_explorer` 文件，并确认组件 fallback 覆盖新路径。
- host 运行崩溃：示例包含 MMIO、无限循环、线程/环境依赖；移除 `allow-run`。

## 推全节奏

1. 先覆盖嵌入式零开销、性能优化和编译链接章节中最需要看汇编的页面。
2. 每页最多放 1-2 个组件，避免页面视觉噪音和读者注意力分散。
3. 每个组件都要服务一个明确教学问题，例如“这个模板是否被内联”“constexpr 是否消掉除法”“虚函数调用是否留下间接跳转”。
4. 完成一批后，用 `rg "<OnlineCompilerDemo" documents` 统计覆盖情况，再决定下一批页面。
