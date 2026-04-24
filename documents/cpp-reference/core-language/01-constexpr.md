---
title: "constexpr"
description: "指示变量或函数的值可以在编译期求值的关键字"
chapter: 99
order: 1
tags:
  - host
  - cpp-modern
  - intermediate
difficulty: intermediate
cpp_standard: [11, 14, 17, 20, 23]
---

# constexpr（C++11）

## 一句话

告诉编译器"这个值或函数有能力在编译期算出来"，从而把运行时的计算挪到编译期，实现零开销的复杂逻辑。

## 头文件

无（语言关键字）

## 核心 API 速查

| 操作 | 签名 | 说明 |
|------|------|------|
| 编译期变量 | `constexpr T var = expr;` | 要求 `expr` 是常量表达式，变量隐式 `const` |
| 编译期函数 | `constexpr T func(params);` | 若参数为常量，则在编译期求值；否则退化为普通函数 |
| 编译期构造 | `constexpr T::T(params);` | 允许在常量表达式中构造字面量类型的对象 |
| 编译期析构 | `constexpr T::~T();` | (C++20) 允许在常量表达式中销毁对象 |
| 特征测试宏 | `__cpp_constexpr` | 检测当前编译器对 constexpr 的支持级别 |

## 最小示例

```cpp
// Standard: C++14
#include <iostream>

constexpr int factorial(int n) {
    int res = 1;
    while (n > 1) res *= n--;
    return res;
}

int main() {
    constexpr int val = factorial(5); // 编译期计算
    std::cout << val << '\n';         // 输出: 120
    int k = 4;
    std::cout << factorial(k) << '\n';// 运行期计算: 24
}
```

## 嵌入式适用性：高

- 将查表、CRC校验、协议解析等计算提前到编译期，不占用 Flash/RAM 空间
- 编译期计算的值可直接用作模板参数（如数组大小），满足裸机环境的静态配置需求
- 相比 C 宏和模板元编程，代码可读性更强，调试体验更好
- 需注意 C++11 限制较多（单 return 语句），建议嵌入式项目至少使用 C++14 标准

## 编译器支持

| GCC | Clang | MSVC |
|-----|-------|------|
| 4.6 | 3.1 | 19.0 |

## 另见

- [cppreference: constexpr specifier](https://en.cppreference.com/w/cpp/language/constexpr)

---
*部分内容参考自 [cppreference.com](https://en.cppreference.com/)，采用 [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) 许可*
