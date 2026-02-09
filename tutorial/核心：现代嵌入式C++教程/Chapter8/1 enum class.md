# 嵌入式C++教程——enum class

想象一下：你把一堆状态、模式、标志写成 `enum`，使用时却被隐式转换成 `int`，结果函数接收错了值、比较错了东西，bug 就笑着出来喝茶。`enum class` 就是 C++ 给你的安全带：强类型、作用域化、能指定底层类型——特别适合对内存、类型安全都有高要求的嵌入式世界。

------

## 一句概念总结

`enum class`（C++11）是强类型、受限作用域的枚举：

- 名字不会污染外部作用域（需要 `E::Val` 访问）；
- 不会隐式转换为整数类型（避免误用）；
- 可以指定底层类型（`uint8_t`、`int16_t` 等），对嵌入式节省空间很有用。

------

## 为什么嵌入式程序员会爱它

1. **类型安全**：防止把不同枚举或 `int` 混到一起，减少逻辑错误。
2. **控制大小**：可以显式声明底层类型，节省 RAM/ROM（比如用 `uint8_t`）。
3. **作用域清晰**：`Status::OK` 不会和 `Error::OK` 撞名。
4. **更易维护**：代码可读性和意图明确，后续审查更少争吵。

------

## 基本例子：老 enum vs enum class

```cpp
// 传统 enum（容易隐式转换）
enum Color { Red, Green, Blue };
void setColor(int c);
setColor(Red); // 隐式转换成 int，有可能传错值

// 强类型枚举
enum class EColor : uint8_t { Red, Green, Blue };
void setColor(EColor c);
setColor(EColor::Red); // 必须显式使用 EColor，安全
```

注意：`enum class` 的默认底层类型是 `int`，但你可以写成 `: uint8_t` 来强制它占 1 字节（对小 MCU 很重要）。

```cpp
static_assert(sizeof(EColor) == 1, "EColor 应该是 1 字节");
```

------

## 常见问题与实战技巧

### 1) 如何输出（打印）枚举值？

`enum class` 不能直接当整数打印，需要 `static_cast`：

```cpp
printf("value = %d\n", static_cast<int>(EColor::Green));
```

或者写个小 helper：

```cpp
template<typename E>
constexpr auto to_underlying(E e) noexcept {
    return static_cast<std::underlying_type_t<E>>(e);
}
```

### 2) 指定底层类型节省内存

在嵌入式中，避免默认 `int`（可能是 32-bit）很重要：

```cpp
enum class SensorState : uint8_t {
    Off = 0,
    Init = 1,
    Ready = 2,
    Error = 3
};
```

用 `uint8_t` 后，变量只占一个字节，struct 排列也更紧凑。

### 3) 与 C 接口互操作

有些底层/库接口要求传 `int` 或 `uint32_t`，这时需要显式转换：

```cpp
extern "C" void hw_set_mode(uint8_t mode);

enum class Mode : uint8_t { Low = 0, High = 1 };
hw_set_mode(static_cast<uint8_t>(Mode::High));
```

### 4) 枚举作为位标志（bitmask）

`enum class` 不支持位运算符默认重载。为可读性与类型安全，可以自己写运算符：

```cpp
#include <type_traits>

template<typename E>
constexpr auto to_ut(E e) noexcept {
    return static_cast<std::underlying_type_t<E>>(e);
}

enum class Flags : uint8_t {
    None = 0,
    Read = 1 << 0,
    Write = 1 << 1,
    Exec = 1 << 2
};

inline Flags operator|(Flags a, Flags b) {
    return static_cast<Flags>(to_ut(a) | to_ut(b));
}
inline Flags& operator|=(Flags& a, Flags b) {
    a = a | b;
    return a;
}
inline Flags operator&(Flags a, Flags b) {
    return static_cast<Flags>(to_ut(a) & to_ut(b));
}
inline bool any(Flags f) { return to_ut(f) != 0; }

// 使用
Flags perms = Flags::Read | Flags::Write;
if (any(perms & Flags::Write)) { /* 有写权限 */ }
```

许多项目会把这些运算符放在头文件并配一套宏或模板自动生成，方便且类型安全。

### 5) switch 语句的提醒

`switch` 仍然可用，但若没有处理所有枚举值，编译器警告（如 `-Wswitch`）会很有用。`enum class` 值要用 `E::V`：

```cpp
switch (state) {
case SensorState::Off:  break;
case SensorState::Init: break;
case SensorState::Ready: break;
case SensorState::Error: break;
}
```

加上 `default` 会抹去某些警告；有时候想利用编译器帮你检查穷尽性，就不要写 `default`，这样缺少分支会被提示。