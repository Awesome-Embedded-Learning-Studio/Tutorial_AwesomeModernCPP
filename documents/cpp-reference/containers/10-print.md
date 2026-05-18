---
title: "std::print"
description: "类型安全的格式化输出到 stdout，C++ 的新 Hello World"
chapter: 99
order: 10
tags:
  - host
  - cpp-modern
  - beginner
difficulty: beginner
cpp_standard: [23]
---

<!--
参考卡模板 (Reference Card Template)
用于 documents/cpp-reference/ 下的特性速查页。
与 article-template.md 不同，参考卡走精炼的结构化格式，不需要叙事风格。

标签使用规则：
1. 必须包含 1 个 platform 标签（参考卡统一用 host）
2. 必须包含 1 个 difficulty 标签
3. 至少包含 1 个 topic 标签
4. 从 scripts/validate_frontmatter.py 的 VALID_TAGS 集合中选取
-->

# std::print（C++23）

## 一句话

直接把格式化字符串输出到 `stdout`——`std::format` + `std::cout` 的合体，C++23 的新 Hello World 写法。

## 头文件

`#include <print>`

## 核心 API 速查

| 操作 | 签名 | 说明 |
|------|------|------|
| 输出到 stdout | `void print(format_string, args...)` | 格式化并输出到标准输出 |
| 输出并换行 | `void println(format_string, args...)` | 自动追加换行符 |
| 空行 | `void println()` | 仅输出一个换行符 |
| 输出到文件 | `void print(FILE* f, format_string, args...)` | 输出到指定 C 文件流 |
| 输出到文件并换行 | `void println(FILE* f, format_string, args...)` | 换行版 |
| 输出到流 | `void vprint_unicode(std::ostream&, ...)` | 输出到 C++ 流 |

## 最小示例

```cpp
// Standard: C++23
#include <print>

int main() {
    std::print("Hello, {}!\n", "world");
    std::println("value = {}", 42);
    std::println("{:>10.2f}", 3.14159); //       3.14
    std::println();                      // 空行
}
```

## 嵌入式适用性：低

- 依赖 `stdout` 和文件系统抽象层，裸机环境通常没有标准输出
- 适用于嵌入式 Linux 上位机工具、测试框架的日志输出
- 格式化引擎的 Flash 开销较大，资源极度受限设备不建议引入
- 可用 `{fmt}` 库的 `fmt::print` 作为 C++11 起的后备方案

## 编译器支持

| GCC | Clang | MSVC |
|-----|-------|------|
| 14 | 18 | 19.34 |

## 另见

- [cppreference: std::print](https://en.cppreference.com/w/cpp/io/print)

---

*部分内容参考自 [cppreference.com](https://en.cppreference.com/)，采用 [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) 许可*
