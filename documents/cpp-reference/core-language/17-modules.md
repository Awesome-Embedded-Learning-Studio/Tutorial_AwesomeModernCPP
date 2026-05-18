---
title: "Modules（模块）"
description: "替代头文件的编译单元机制：更快编译、更好的封装、宏隔离"
chapter: 99
order: 17
tags:
  - host
  - cpp-modern
  - intermediate
difficulty: intermediate
cpp_standard: [20, 23]
---

<!--
参考卡模板 (Reference Card Template)
用于 documents/cpp-reference/ 下的特性速查页。
与 article-template.md 不同，参考卡走精查的结构化格式，不需要叙事风格。

标签使用规则：
1. 必须包含 1 个 platform 标签（参考卡统一用 host）
2. 必须包含 1 个 difficulty 标签
3. 至少包含 1 个 topic 标签
4. 从 scripts/validate_frontmatter.py 的 VALID_TAGS 集合中选取
-->

# Modules 模块（C++20）

## 一句话

用模块接口文件（`.cppm`）替代头文件——编译一次后缓存结果，大幅加速重编译，同时隔离宏污染并提供真正的符号可见性控制。

## 头文件

无（语言特性，使用新的文件类型和关键字）

## 核心 API 速查

| 语法 | 说明 |
|------|------|
| `module;` | 全局模块片段起始（放 `#include` 等预处理指令） |
| `export module mylib;` | 声明模块接口单元，导出模块名 `mylib` |
| `export int func();` | 导出声明，对模块使用者可见 |
| `module mylib;` | 模块实现单元（不导出，仅实现） |
| `import mylib;` | 导入模块（替代 `#include`） |
| `export import :sub;` | 再导出子模块 |
| `module :private;` | 私有模块片段（C++20），实现细节不参与模块接口 |

## 最小示例

```cpp
// Standard: C++20
// --- math.cppm (模块接口) ---
export module math;

export int add(int a, int b) {
    return a + b;
}

// --- main.cpp (使用者) ---
import math;
#include <iostream>

int main() {
    std::cout << add(2, 3) << "\n"; // 5
}
```

## 嵌入式适用性：中

- 编译加速：模块接口编译一次后缓存，大型项目重编译时间可减少 30-70%
- 宏隔离：模块边界外部的 `#define` 不会泄漏进模块内部，提升构建稳定性
- 符号可见性：`export` 显式控制 API 边界，替代头文件"一切公开"的模式
- 构建系统支持尚不完善：CMake 对 modules 的原生支持在 3.28+ 逐步完善
- 各编译器实现存在兼容性问题（模块 BMI 格式不通用），跨编译器构建需谨慎
- 嵌入式工具链（尤其交叉编译场景）modules 支持滞后，短期不建议在嵌入式项目核心中采用

## 编译器支持

| GCC | Clang | MSVC |
|-----|-------|------|
| 11 | 16 | 19.28 |

## 另见

- [cppreference: Modules](https://en.cppreference.com/w/cpp/language/modules)

---

*部分内容参考自 [cppreference.com](https://en.cppreference.com/)，采用 [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) 许可*
