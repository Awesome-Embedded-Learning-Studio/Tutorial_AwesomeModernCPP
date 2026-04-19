---
title: "可变参数模板"
description: "接受零个或多个模板参数或函数参数的模板机制"
chapter: 99
order: 2
tags:
  - host
  - cpp-modern
  - intermediate
difficulty: intermediate
cpp_standard: [11, 14, 17, 20, 23]
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

# 可变参数模板（C++11）

## 一句话

允许模板接受任意数量、任意类型的参数，是类型安全地替代 C 风格可变参数（`va_list`）的现代方案。

## 头文件

无需头文件（语言特性）

## 核心 API 速查

| 操作 | 签名 | 说明 |
|------|------|------|
| 类型参数包 | `typename... Ts` | 接受零个或多个类型参数 |
| 非类型参数包 | `Ts... args` | 接受零个或多个非类型参数 |
| 模板模板参数包 | `template<typename> class... Ts` | 接受零个或多个模板 |
| 参数包展开 | `args...` | 将参数包展开为多个表达式 |
| 参数包大小 | `sizeof...(args)` | 返回参数包中的元素数量 |
| 折叠表达式 | `(args op ...)` / `(... op args)` | C++17，对参数包执行逐元运算 |

## 最小示例

```cpp
// Standard: C++11
#include <iostream>

template<typename... Ts>
void print(Ts... args) {
    // 利用初始化列表保证顺序地逐个打印
    int dummy[] = {(std::cout << args << " ", 0)...};
    (void)dummy;
}

int main() {
    print(1, "hello", 3.14);
}
```

## 嵌入式适用性：中

- 可完全替代不安全的 `va_list`，提升类型安全性与代码可维护性
- 模板实例化会导致代码膨胀（二进制体积增加），需关注 Flash 占用
- 适合资源较充足的场景（如带 Linux 的应用处理器），裸机低端 MCU 上需谨慎评估

## 编译器支持

| GCC | Clang | MSVC |
|-----|-------|------|
| 4.3 | 2.9   | 待补充 |

## 另见

- [cppreference: Parameter packs](https://en.cppreference.com/w/cpp/language/parameter_pack)

---

*部分内容参考自 [cppreference.com](https://en.cppreference.com/)，采用 [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) 许可*
