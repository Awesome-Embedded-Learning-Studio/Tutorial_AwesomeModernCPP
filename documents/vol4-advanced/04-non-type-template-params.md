---
title: "非类型模板参数"
description: "深入理解C++非类型模板参数的用法与嵌入式应用"
chapter: 12
order: 4
tags:
  - cpp-modern
  - host
  - intermediate
difficulty: intermediate
reading_time_minutes: 35
prerequisites:
  - "Chapter 12: 模板入门概述"
  - "Chapter 12: 函数模板详解"
cpp_standard: [11, 14, 17, 20]
platform: host
---

# 嵌入式现代C++教程——非类型模板参数

## 引言：编译期的魔法数字

在嵌入式开发中，我们经常需要处理各种"魔法数字"——寄存器地址、位掩码、缓冲区大小、波特率等。传统做法是使用宏或常量：

```cpp
#define UART0_BASE 0x4000C000
#define BUFFER_SIZE 256
#define BAUD_RATE 115200

uint8_t buffer[BUFFER_SIZE];
```

这种方式有几个问题：

1. **没有类型安全**：宏只是文本替换，编译器不做类型检查
2. **调试困难**：宏在预处理阶段就被替换，调试时看不到原始名称
3. **作用域混乱**：宏没有作用域，容易冲突
4. **无法编译期计算**：不能基于这些值进行复杂的编译期计算

C++的非类型模板参数提供了一种更优雅的解决方案。它允许你用**编译期常量**作为模板参数，在保持类型安全的同时，实现零开销的抽象。

------

## 非类型模板参数基础

### 什么是非类型模板参数？

模板参数不仅可以是类型（`typename T`），还可以是编译期常量值：

```cpp
template<typename T, std::size_t N>  // T是类型参数，N是非类型参数
class Array {
    T data[N];
};
```

**关键特性**：

- 非类型模板参数是**编译期常量**
- 它们的值在模板实例化时必须确定
- 编译器会为不同的值生成不同的模板实例

### 支持的类型

C++标准限定了非类型模板参数可以使用的类型：

| C++标准 | 支持的类型 |
|---------|-----------|
| C++11/14 | 整型、枚举、指针、引用、指向成员的指针 |
| C++17 | 增加自动类型推导（`auto`） |
| C++20 | 增加浮点型、类对象（需满足特定条件） |

```cpp
// 整型
template<int Value>
struct IntConstant { };

// 枚举
template<enum MyEnum E>
struct EnumConstant { };

// 指针
template<const char* Ptr>
struct PointerConstant { };

// 引用
template<int& Ref>
struct ReferenceConstant { };

// C++17: auto
template<auto Value>
struct AutoConstant { };

// C++20: 浮点型
template<double Value>
struct DoubleConstant { };
```

------

## 整型非类型模板参数

### 基本用法

整型是最常用的非类型模板参数类型，包括：

- `bool`、`char`、`int`、`unsigned int` 等
- `std::size_t`、`int8_t`、`uint16_t` 等固定宽度类型

```cpp
template<int Value>
struct IntegralConstant {
    static constexpr int value = Value;
    using value_type = int;

    constexpr operator int() const { return value; }
};

// 使用
using Five = IntegralConstant<5>;
static_assert(Five::value == 5);
```

### 编译期数组大小

这是非类型模板参数最常见的应用场景：

```cpp
template<typename T, std::size_t N>
class FixedArray {
public:
    constexpr std::size_t size() const { return N; }
    constexpr bool empty() const { return N == 0; }

    T& operator[](std::size_t index) {
        return data[index];
    }

    const T& operator[](std::size_t index) const {
        return data[index];
    }

    T* begin() { return data; }
    T* end() { return data + N; }
    const T* begin() const { return data; }
    const T* end() const { return data + N; }

private:
    T data[N];
};

// 使用
FixedArray<int, 10> arr1;  // 编译期确定大小，栈上分配
FixedArray<uint8_t, 32> arr2;

static_assert(arr1.size() == 10);
```

**优势**：

- 编译期确定大小，无需动态分配
- 边界检查可以在编译期进行（配合`static_assert`）
- 内存布局完全可预测

### 位掩码生成器

在嵌入式开发中，我们经常需要操作寄存器的特定位：

```cpp
template<typename RegisterType, RegisterType BitPos>
struct BitMask {
    static_assert(BitPos < sizeof(RegisterType) * 8, "Bit position out of range");

    static constexpr RegisterType mask = static_cast<RegisterType>(1) << BitPos;

    // 设置位
    static constexpr RegisterType set(RegisterType reg) {
        return reg | mask;
    }

    // 清除位
    static constexpr RegisterType clear(RegisterType reg) {
        return reg & ~mask;
    }

    // 切换位
    static constexpr RegisterType toggle(RegisterType reg) {
        return reg ^ mask;
    }

    // 测试位
    static constexpr bool is_set(RegisterType reg) {
        return (reg & mask) != 0;
    }
};

// 使用场景：GPIO配置
using GPIO_Pin5 = BitMask<uint32_t, 5>;
using GPIO_Pin13 = BitMask<uint32_t, 13>;

uint32_t gpio_mode = 0;
gpio_mode = GPIO_Pin5::set(gpio_mode);     // 设置第5位
gpio_mode = GPIO_Pin13::clear(gpio_mode);  // 清除第13位

if (GPIO_Pin5::is_set(gpio_mode)) {
    // 第5位已设置
}
```

### 多位掩码

```cpp
template<typename RegisterType, RegisterType HighBit, RegisterType LowBit>
struct BitFieldMask {
    static_assert(HighBit >= LowBit, "High bit must be >= Low bit");
    static_assert(HighBit < sizeof(RegisterType) * 8, "High bit out of range");

    static constexpr RegisterType width = HighBit - LowBit + 1;
    static constexpr RegisterType mask = ((static_cast<RegisterType>(1) << width) - 1) << LowBit;

    // 提取位域
    static constexpr RegisterType extract(RegisterType reg) {
        return (reg & mask) >> LowBit;
    }

    // 设置位域
    static constexpr RegisterType set(RegisterType reg, RegisterType value) {
        return (reg & ~mask) | ((value << LowBit) & mask);
    }
};

// 使用：UART波特率设置（通常在特定位域）
using UART_BaudField = BitFieldMask<uint32_t, 15, 0>;

uint32_t uart_ctrl = 0;
uart_ctrl = UART_BaudField::set(uart_ctrl, 115200);  // 设置波特率
```

### 编译期查找表

```cpp
template<typename T, std::size_t Size>
class LookupTable {
public:
    constexpr LookupTable(const T (&values)[Size]) {
        for (std::size_t i = 0; i < Size; ++i) {
            data[i] = values[i];
        }
    }

    constexpr T operator[](std::size_t index) const {
        return data[index];
    }

    constexpr std::size_t size() const { return Size; }

private:
    T data[Size];
};

// 编译期生成正弦表
constexpr double generate_sin(std::size_t i, std::size_t total) {
    return std::sin(2.0 * 3.14159265358979323846 * i / total);
}

template<std::size_t Size>
struct SineTable {
    constexpr SineTable() : values{} {
        for (std::size_t i = 0; i < Size; ++i) {
            values[i] = generate_sin(i, Size);
        }
    }

    double values[Size];
};

constexpr SineTable<256> sine_table;  // 编译期生成256点正弦表
```

### 限制与注意事项

**整型限制**：

1. **必须是编译期常量**

    ```cpp
    constexpr int N = 10;
    FixedArray<int, N> arr1;  // OK

    int M = 20;
    FixedArray<int, M> arr2;  // 错误：M不是编译期常量
    ```

2. **大小限制**

    ```cpp
    template<std::size_t N>
    class LargeBuffer {
        uint8_t data[N];  // N不能太大，否则栈溢出
    };

    LargeBuffer<1024> buf1;   // OK
    LargeBuffer<1024*1024> buf2;  // 可能栈溢出
    ```

3. **符号问题**

    ```cpp
    template<int N>  // 有符号
    struct SignedBuf { };

    template<unsigned int N>  // 无符号
    struct UnsignedBuf { };

    SignedBuf<-1> sb;   // OK
    // UnsignedBuf<-1> ub;  // 错误：无符号不能为负
    ```

------

## 指针和引用类型参数

### 指针类型参数

指针类型参数允许你在编译期绑定到全局对象：

```cpp
// 全局数组
constexpr char device_name[] = "STM32F407";

template<const char* Name>
struct Device {
    static constexpr const char* get_name() { return Name; }
};

// 使用
using MyDevice = Device<device_name>;
static_assert(std::string_view{MyDevice::get_name()} == "STM32F407");
```

**注意**：指针参数必须指向具有外部链接的对象（全局/静态变量）。

### 更实用的指针参数场景：寄存器地址映射

这是非类型模板参数在嵌入式中最重要的应用之一：

```cpp
// 寄存器基类
template<typename RegType, uintptr_t Address>
struct Register {
    using value_type = RegType;

    // 读寄存器
    static RegType read() {
        return *reinterpret_cast<volatile RegType*>(Address);
    }

    // 写寄存器
    static void write(RegType value) {
        *reinterpret_cast<volatile RegType*>(Address) = value;
    }

    // 设置位
    static void set(RegType mask) {
        *reinterpret_cast<volatile RegType*>(Address) |= mask;
    }

    // 清除位
    static void clear(RegType mask) {
        *reinterpret_cast<volatile RegType*>(Address) &= ~mask;
    }

    // 地址访问
    static constexpr uintptr_t address() { return Address; }
};

// STM32 GPIO寄存器定义
using GPIOA_BASE = Register<uint32_t, 0x40020000>;
using GPIOB_BASE = Register<uint32_t, 0x40020400>;

// GPIO模式寄存器偏移
template<typename Base, uintptr_t Offset>
struct GPIO_MODER : public Register<uint32_t, Base::address() + Offset> {
    using BaseReg = Base;
    static constexpr uintptr_t offset = Offset;
};

// 使用
using GPIOA_MODER = GPIO_MODER<GPIOA_BASE, 0x00>;

// 配置PA5为输出（GPIO模式寄存器中第10-11位）
void config_pa5_output() {
    constexpr uint32_t mode_mask = 0x3 << 10;  // PA5对应位
    constexpr uint32_t output_mode = 0x1 << 10;
    uint32_t current = GPIOA_MODER::read();
    GPIOA_MODER::write((current & ~mode_mask) | output_mode);
}
```

### 引用类型参数

引用类型参数与指针类似，但提供了更自然的语法：

```cpp
constexpr int global_config = 42;

template<int& Config>
struct ConfigReader {
    static int get() { return Config; }
    static void set(int value) { Config = value; }
};

// 使用
using MyConfig = ConfigReader<global_config>;
```

### 指向成员的指针

虽然较少使用，但有时也很有用：

```cpp
struct SensorData {
    int temperature;
    int humidity;
    int pressure;
};

template<int SensorData::* Member>
struct SensorField {
    static int read(const SensorData& data) {
        return data.*Member;
    }

    static void write(SensorData& data, int value) {
        data.*Member = value;
    }
};

// 使用
using TempField = SensorField<&SensorData::temperature>;
using HumidField = SensorField<&SensorData::humidity>;

SensorData data{};
TempField::write(data, 25);  // 设置温度
```

### 限制

**指针/引用参数的限制**：

1. **必须指向/引用全局/静态对象**

    ```cpp
    template<int* Ptr>
    struct PtrHolder { };

    int global_var;
    static int static_var;

    void func() {
        int local_var;
        PtrHolder<&global_var> h1;    // OK
        PtrHolder<&static_var> h2;    // OK
        // PtrHolder<&local_var> h3;   // 错误：局部变量
    }
    ```

2. **字符串字面量的特殊性**

    ```cpp
    template<const char* Str>
    struct StringHolder { };

    // 字符串字面量具有内部链接，C++中需要特殊处理
    extern constexpr char str[] = "hello";  // extern赋予外部链接
    StringHolder<str> sh;  // OK
    ```

3. **nullptr的特殊处理**

    ```cpp
    template<const char* Ptr>
    struct PtrTest { };

    // PtrTest<nullptr> pt;  // 错误：nullptr不能直接用于指针模板参数
    // 正确做法：使用0或nullptr_t模板参数
    template<std::nullptr_t>
    struct NullPtrTest { };

    NullPtrTest<nullptr> npt;  // OK
    ```

------

## C++17：auto非类型模板参数

### 基本语法

C++17引入了`auto`作为非类型模板参数类型，让编译器自动推导：

```cpp
template<auto Value>
struct AutoConstant {
    static constexpr auto value = Value;
    using value_type = decltype(Value);
};

// 使用
using IntVal = AutoConstant<42>;        // 推导为int
using CharVal = AutoConstant<'x'>;      // 推导为char
using UintVal = AutoConstant<123U>;     // 推导为unsigned int
using PtrVal = AutoConstant<nullptr>;   // 推导为std::nullptr_t
```

### 类型推导规则

```cpp
template<auto Value>
void print_value() {
    std::cout << Value << " (type: " << typeid(decltype(Value)).name() << ")\n";
}

print_value<42>();      // int
print_value<42L>();     // long
print_value<42U>();     // unsigned int
print_value<3.14>();    // double
print_value<'a'>();     // char
```

### 实际应用：通用值包装器

```cpp
template<auto Value>
struct ValueHolder {
    constexpr static auto value = Value;
    constexpr operator decltype(Value)() const { return Value; }

    // 基于值类型的操作
    constexpr auto square() const {
        return Value * Value;
    }
};

// 使用
constexpr auto v1 = ValueHolder<10>{};
static_assert(v1.value == 10);
static_assert(v1.square() == 100);
```

### 类型安全的枚举包装

```cpp
enum class Color : uint8_t {
    Red = 0,
    Green = 1,
    Blue = 2
};

template<auto ColorValue>
struct ColorInfo {
    static_assert(std::is_same_v<decltype(ColorValue), const Color>,
                  "Must be a Color enum value");

    static constexpr Color color = ColorValue;
    static constexpr const char* name() {
        if constexpr (color == Color::Red) return "Red";
        else if constexpr (color == Color::Green) return "Green";
        else if constexpr (color == Color::Blue) return "Blue";
        else return "Unknown";
    }
};

// 使用
using RedColor = ColorInfo<Color::Red>;
static_assert(std::string_view{RedColor::name()} == "Red");
```

### 编译期值序列

```cpp
template<auto... Values>
struct ValueSequence {
    static constexpr std::size_t size = sizeof...(Values);

    template<std::size_t Index>
    static constexpr auto get = std::get<Index>(std::make_tuple(Values...));
};

// 使用
using MySequence = ValueSequence<1, 2.0, 'c', nullptr>;
static_assert(MySequence::size == 4);
static_assert(MySequence::get<0> == 1);
```

### auto参数的限制

```cpp
// ❌ 不能作为函数参数的默认模板参数
template<auto Value = 10>  // C++17允许，但使用时需要注意
struct DefaultAuto { };

DefaultAuto<> da;  // OK

// ❌ 不能推导复杂的类型
struct MyType { int x; };
// template<auto Value = MyType{42}>  // 错误：非字面量类型
// struct ComplexAuto { };

// ✅ C++20后，字面量类类型也可以使用
template<auto Value>
struct LiteralAuto { };

// C++20允许，如果MyType是字面量类型
struct MyType {
    int x;
    constexpr MyType(int v) : x(v) {}
};

// LiteralAuto<MyType{42}> la;  // C++20: OK
```

------

## 实战：编译期数组大小工具

### 问题背景

在C/C++中，数组作为函数参数会退化为指针，丢失大小信息：

```cpp
void func(int arr[]) {
    // sizeof(arr)是指针大小，不是数组大小！
    // 无法获取原始数组大小
}

int data[100];
func(data);  // 丢失大小信息
```

传统C宏的做法存在安全问题：

```cpp
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

// 问题：对指针使用会得到错误结果
int* ptr = nullptr;
size_t wrong = ARRAY_SIZE(ptr);  // 编译通过，但结果错误
```

### 模板解决方案

```cpp
// 基础版本
template<typename T, std::size_t N>
constexpr std::size_t array_size(T (&)[N]) noexcept {
    return N;
}

// 使用
int data[42];
constexpr auto size = array_size(data);  // 42，编译期常量

// array_size(ptr);  // 编译错误！指针不能匹配
```

### 增强版：类型安全的数组工具

```cpp
template<typename T, std::size_t N>
struct ArrayInfo {
    using value_type = T;
    using pointer = T*;
    using const_pointer = const T*;
    using reference = T&;
    using const_reference = const T&;
    using size_type = std::size_t;

    static constexpr size_type size = N;
    static constexpr size_type bytes = sizeof(T) * N;

    // 编译期边界检查
    template<size_type Index>
    static constexpr size_type safe_index = Index < N ? Index :
        throw "Index out of bounds";  // 编译期错误

    // 元素访问（编译期索引）
    template<size_type Index>
    static constexpr reference get(T (&arr)[N]) {
        static_assert(Index < N, "Index out of bounds");
        return arr[Index];
    }

    // 字节大小
    static constexpr size_type byte_size() noexcept {
        return bytes;
    }
};

// 使用
int data[10];
using Info = ArrayInfo<int, 10>;

static_assert(Info::size == 10);
static_assert(Info::bytes == 40);
static_assert(Info::safe_index<5> == 5);
// static_assert(Info::safe_index<15> == 15);  // 编译错误
```

### 多维数组支持

```cpp
// 一维数组
template<typename T, std::size_t N>
constexpr std::size_t array_size(T (&)[N]) noexcept {
    return N;
}

// 二维数组
template<typename T, std::size_t M, std::size_t N>
constexpr std::size_t array_size(T (&)[M][N]) noexcept {
    return M * N;
}

// 通用版：返回总元素数
template<typename T, std::size_t N>
constexpr std::size_t array_total_size(T (&)[N]) noexcept {
    return N;
}

template<typename T, std::size_t M, std::size_t... Ns>
constexpr std::size_t array_total_size(T (&)[M][Ns...]) noexcept {
    return M * array_total_size(((T(&)[Ns...])nullptr)[0]);
}

// 获取维度
template<typename T>
struct ArrayRank;

template<typename T, std::size_t N>
struct ArrayRank<T[N]> : std::integral_constant<std::size_t, 1> {};

template<typename T, std::size_t N, std::size_t... Ns>
struct ArrayRank<T[N][Ns...]> : std::integral_constant<std::size_t,
    1 + ArrayRank<T[Ns...]>::value> {};

// 使用
int arr2d[3][4];
static_assert(array_size(arr2d) == 3 * 4);  // 总元素数
static_assert(ArrayRank<decltype(arr2d)>::value == 2);  // 维度
```

### 编译期数组迭代器

```cpp
template<typename T, std::size_t N>
class ArrayIterator {
public:
    constexpr ArrayIterator(T (&arr)[N], std::size_t pos = 0)
        : array_(arr), pos_(pos) {}

    constexpr T& operator*() const {
        return array_[pos_];
    }

    constexpr ArrayIterator& operator++() {
        ++pos_;
        return *this;
    }

    constexpr ArrayIterator operator++(int) {
        auto tmp = *this;
        ++pos_;
        return tmp;
    }

    constexpr bool operator!=(const ArrayIterator& other) const {
        return pos_ != other.pos_;
    }

private:
    T (&array_)[N];
    std::size_t pos_;
};

template<typename T, std::size_t N>
struct ArrayRange {
    T (&array)[N];

    constexpr ArrayIterator<T, N> begin() const {
        return {array, 0};
    }

    constexpr ArrayIterator<T, N> end() const {
        return {array, N};
    }
};

// 使用
int data[5] = {1, 2, 3, 4, 5};
constexpr auto range = ArrayRange{data};

// 编译期遍历
constexpr bool all_positive = [] {
    for (auto it = range.begin(); it != range.end(); ++it) {
        if (*it <= 0) return false;
    }
    return true;
}();
static_assert(all_positive);
```

### 完整示例：嵌入式缓冲区工具

<details>
<summary>点击展开完整的编译期缓冲区工具代码</summary>

```cpp
#include <cstdint>
#include <cstddef>

// 基础工具
template<typename T, std::size_t N>
constexpr std::size_t array_size(T (&)[N]) noexcept {
    return N;
}

// 编译期缓冲区类
template<typename T, std::size_t Size, std::size_t Alignment = alignof(T)>
class AlignedBuffer {
    alignas(Alignment) T data_[Size];
public:
    using value_type = T;
    using size_type = std::size_t;
    using pointer = T*;
    using const_pointer = const T*;

    static constexpr size_type size = Size;
    static constexpr size_type alignment = Alignment;
    static constexpr size_type bytes = sizeof(T) * Size;

    // 编译期初始化为特定值
    constexpr AlignedBuffer() : data_{} {}

    constexpr AlignedBuffer(T init_value) : data_{} {
        for (size_type i = 0; i < Size; ++i) {
            data_[i] = init_value;
        }
    }

    // 元素访问
    constexpr T& operator[](size_type index) {
        return data_[index];
    }

    constexpr const T& operator[](size_type index) const {
        return data_[index];
    }

    // 指针访问
    constexpr pointer data() { return data_; }
    constexpr const_pointer data() const { return data_; }

    // 填充
    constexpr void fill(T value) {
        for (size_type i = 0; i < Size; ++i) {
            data_[i] = value;
        }
    }

    // 比较编译期大小
    template<std::size_t OtherSize>
    constexpr bool size_equals() const {
        return Size == OtherSize;
    }
};

// 环形缓冲区（编译期容量）
template<typename T, std::size_t Capacity>
class CircularBuffer {
    T data_[Capacity];
    std::size_t head_ = 0;
    std::size_t tail_ = 0;
    bool full_ = false;

public:
    static constexpr std::size_t capacity = Capacity;
    using value_type = T;

    constexpr CircularBuffer() : data_{} {}

    constexpr bool push(T value) {
        data_[head_] = value;
        if (full_) {
            tail_ = (tail_ + 1) % Capacity;
        }
        head_ = (head_ + 1) % Capacity;
        full_ = (head_ == tail_);
        return true;
    }

    constexpr bool pop(T& value) {
        if (empty()) return false;
        value = data_[tail_];
        full_ = false;
        tail_ = (tail_ + 1) % Capacity;
        return true;
    }

    constexpr bool empty() const {
        return !full_ && (head_ == tail_);
    }

    constexpr bool full() const {
        return full_;
    }

    constexpr std::size_t size() const {
        if (full_) return Capacity;
        return (head_ >= tail_) ? (head_ - tail_) :
               (Capacity + head_ - tail_);
    }

    constexpr void clear() {
        head_ = tail_ = 0;
        full_ = false;
    }
};

// 使用示例
void buffer_example() {
    // 对齐缓冲区
    AlignedBuffer<uint32_t, 16, 16> dma_buffer;  // 16字节对齐，用于DMA
    AlignedBuffer<uint8_t, 256> uart_rx;

    // 环形缓冲区
    CircularBuffer<uint8_t, 32> rx_ring;
    rx_ring.push(0x01);
    rx_ring.push(0x02);

    uint8_t data;
    while (rx_ring.pop(data)) {
        // 处理数据
    }
}

```

</details>

------

## 实战：位掩码生成器

### 编译期位操作工具集

```cpp
#include <type_traits>

namespace bits {

// 基础位掩码
template<typename RegType, RegType Bit>
struct Bit {
    static_assert(Bit < sizeof(RegType) * 8, "Bit out of range");
    static constexpr RegType mask = RegType{1} << Bit;

    static constexpr RegType set(RegType reg) { return reg | mask; }
    static constexpr RegType clear(RegType reg) { return reg & ~mask; }
    static constexpr RegType toggle(RegType reg) { return reg ^ mask; }
    static constexpr bool is_set(RegType reg) { return (reg & mask) != 0; }
};

// 多位掩码
template<typename RegType, RegType High, RegType Low>
struct Bits {
    static_assert(High >= Low, "High must be >= Low");
    static constexpr RegType width = High - Low + 1;
    static constexpr RegType mask = ((RegType{1} << width) - 1) << Low;

    static constexpr RegType extract(RegType reg) {
        return (reg & mask) >> Low;
    }

    static constexpr RegType set(RegType reg, RegType value) {
        return (reg & ~mask) | ((value << Low) & mask);
    }
};

// 编译期计算位掩码值
template<typename RegType, RegType... Bits>
struct BitMask {
    static constexpr RegType value = ((RegType{1} << Bits) | ...);
};

// 寄存器定义辅助
template<typename RegType, RegType Offset, RegType... Bits>
struct RegisterField {
    static constexpr RegType offset = Offset;
    static constexpr RegType mask = ((RegType{1} << Bits) | ...);

    template<typename BaseAddr>
    static constexpr RegType read() {
        return *reinterpret_cast<volatile RegType*>(BaseAddr::address + offset);
    }

    template<typename BaseAddr>
    static constexpr void write(RegType value) {
        *reinterpret_cast<volatile RegType*>(BaseAddr::address + offset) = value;
    }

    template<typename BaseAddr>
    static constexpr void set(RegType value) {
        write<BaseAddr>(read<BaseAddr>() | (value << offset));
    }
};

} // namespace bits

// 使用示例
using namespace bits;

// 定义LED位掩码
using LED1 = Bit<uint32_t, 5>;
using LED2 = Bit<uint32_t, 6>;
using LED3 = Bit<uint32_t, 7>;

void gpio_example() {
    uint32_t gpio_odr = 0;

    // 设置LED
    gpio_odr = LED1::set(gpio_odr);
    gpio_odr = LED2::set(gpio_odr);

    // 测试LED状态
    if (LED1::is_set(gpio_odr)) {
        // LED1打开
    }

    // 切换LED
    gpio_odr = LED3::toggle(gpio_odr);

    // 清除LED
    gpio_odr = LED2::clear(gpio_odr);
}
```

### 编译期位域提取器

```cpp
// 通信协议中的位域解析
template<typename T, int ByteOffset, int BitOffset, int BitWidth>
struct BitField {
    static_assert(BitWidth > 0 && BitWidth <= 64, "Invalid bit width");
    static_assert(BitOffset >= 0 && BitOffset < 8, "Invalid bit offset");

    using value_type = std::conditional_t<BitWidth <= 8, uint8_t,
                        std::conditional_t<BitWidth <= 16, uint16_t,
                        std::conditional_t<BitWidth <= 32, uint32_t, uint64_t>>>;

    static constexpr int total_bit_offset = ByteOffset * 8 + BitOffset;
    static constexpr T mask = ((T{1} << BitWidth) - 1) << total_bit_offset;

    static constexpr value_type extract(const T* buffer) {
        return static_cast<value_type>(
            (buffer[ByteOffset] >> BitOffset) & ((1 << BitWidth) - 1)
        );
    }

    static constexpr void encode(T* buffer, value_type value) {
        buffer[ByteOffset] &= ~(((1 << BitWidth) - 1) << BitOffset);
        buffer[ByteOffset] |= (value & ((1 << BitWidth) - 1)) << BitOffset;
    }
};

// 使用：CAN消息解析
uint8_t can_msg[8];

// 假设某信号位于字节2的第3位，长度5位
using CanSignal = BitField<uint8_t, 2, 3, 5>;

auto signal_value = CanSignal::extract(can_msg);  // 提取信号
CanSignal::encode(can_msg, 0x1F);                 // 编码信号
```

### SPI/I2C配置生成器

```cpp
// SPI配置参数
template<int Prescaler, int CPOL, int CPHA, int BitOrder>
struct SPIConfig {
    static_assert(Prescaler >= 2 && Prescaler <= 256, "Invalid prescaler");
    static_assert(CPOL == 0 || CPOL == 1, "CPOL must be 0 or 1");
    static_assert(CPHA == 0 || CPHA == 1, "CPHA must be 0 or 1");
    static_assert(BitOrder == 0 || BitOrder == 1, "BitOrder must be MSB_FIRST(0) or LSB_FIRST(1)");

    // 计算控制寄存器值（简化版）
    static constexpr uint32_t cr1_value =
        (BitOrder << 7) |
        (CPHA << 0) |
        (CPOL << 1) |
        ((Prescaler / 2 - 1) << 3);  // 假设prescaler编码
};

// 预定义配置
using SPI_FullSpeed_CPOL0_CPHA0 = SPIConfig<2, 0, 0, 0>;
using SPI_HalfSpeed_CPOL1_CPHA1 = SPIConfig<4, 1, 1, 0>;

// I2C速率配置
template<int ClockRate, int RiseTime, int FallTime>
struct I2CTiming {
    static constexpr int standard_freq = 100000;    // 100kHz
    static constexpr int fast_freq = 400000;        // 400kHz
    static constexpr int fast_plus_freq = 1000000;  // 1MHz

    static constexpr bool is_standard = ClockRate == standard_freq;
    static constexpr bool is_fast = ClockRate == fast_freq;
    static constexpr bool is_fast_plus = ClockRate == fast_plus_freq;

    // 计算时序寄存器值（简化）
    static constexpr uint32_t timingr_value =
        (is_standard ? 0x00000000 : is_fast ? 0x00600000 : 0x00100000);
};

using I2C_StandardMode = I2CTiming<100000, 150, 50>;
using I2C_FastMode = I2CTiming<400000, 100, 30>;
```

------

## 嵌入式应用：寄存器地址的编译期封装

### 类型安全的寄存器访问

```cpp
#include <cstdint>
#include <type_traits>

namespace hal {

// 寄存器地址标记类型
template<typename Tag>
struct RegisterAddress {
    uintptr_t address;
    constexpr explicit RegisterAddress(uintptr_t addr) : address(addr) {}
};

// 寄存器基类
template<typename RegType, typename Tag>
class Register {
public:
    using value_type = RegType;

    explicit constexpr Register(RegisterAddress<Tag> addr)
        : address_(reinterpret_cast<volatile RegType*>(addr.address)) {}

    // 读操作
    constexpr RegType read() const volatile {
        return *address_;
    }

    // 写操作
    constexpr void write(RegType value) volatile {
        *address_ = value;
    }

    // 设置位
    constexpr void set(RegType mask) volatile {
        *address_ |= mask;
    }

    // 清除位
    constexpr void clear(RegType mask) volatile {
        *address_ &= ~mask;
    }

    // 切换位
    constexpr void toggle(RegType mask) volatile {
        *address_ ^= mask;
    }

    // 读-修改-写
    constexpr void modify(RegType clear_mask, RegType set_mask) volatile {
        *address_ = (*address_ & ~clear_mask) | set_mask;
    }

private:
    volatile RegType* const address_;
};

// 标签类型（防止类型混淆）
struct GPIOA_Tag {};
struct GPIOB_Tag {};
struct UART1_Tag {};
struct SPI1_Tag {};

// 基地址定义
namespace base {
    constexpr inline RegisterAddress<GPIOA_Tag> GPIOA{0x40020000};
    constexpr inline RegisterAddress<GPIOB_Tag> GPIOB{0x40020400};
    constexpr inline RegisterAddress<UART1_Tag> UART1{0x40011000};
    constexpr inline RegisterAddress<SPI1_Tag> SPI1{0x40013000};
}

// 具体寄存器定义（使用偏移量）
template<typename BaseTag, uintptr_t Offset>
using GPIO_Register = Register<uint32_t,
    struct { using tag = BaseTag; constexpr static uintptr_t offset = Offset; }>;

} // namespace hal
```

### 外设定义框架

```cpp
namespace stm32f4 {

// GPIO端口定义
template<typename Tag>
struct GPIO_Port {
    using MODER = Register<uint32_t, RegisterAddress<Tag>{0x40020000}>;
    using OTYPER = Register<uint16_t, RegisterAddress<Tag>{0x40020004}>;
    using OSPEEDR = Register<uint32_t, RegisterAddress<Tag>{0x40020008}>;
    using PUPDR = Register<uint32_t, RegisterAddress<Tag>{0x4002000C}>;
    using IDR = Register<uint16_t, RegisterAddress<Tag>{0x40020010}>;
    using ODR = Register<uint16_t, RegisterAddress<Tag>{0x40020014}>;
    using BSRR = Register<uint32_t, RegisterAddress<Tag>{0x40020018}>;

    // 模式定义
    enum Mode : uint32_t {
        Input = 0,
        Output = 1,
        Alternate = 2,
        Analog = 3
    };

    // 输出类型
    enum OutputType : uint16_t {
        PushPull = 0,
        OpenDrain = 1
    };

    // 速度
    enum Speed : uint32_t {
        Low = 0,
        Medium = 1,
        High = 2,
        VeryHigh = 3
    };

    // 上下拉
    enum Pull : uint32_t {
        None = 0,
        Up = 1,
        Down = 2
    };

    // 引脚配置辅助函数
    template<int Pin>
    static void config_mode(Mode mode) {
        constexpr uint32_t mask = 0x3 << (Pin * 2);
        MODER reg{RegisterAddress<Tag>{0x40020000}};
        reg.modify(mask, static_cast<uint32_t>(mode) << (Pin * 2));
    }

    template<int Pin>
    static void config_output(OutputType type) {
        OTYPER reg{RegisterAddress<Tag>{0x40020004}};
        if (type == OpenDrain) {
            reg.set(1 << Pin);
        } else {
            reg.clear(1 << Pin);
        }
    }
};

// 具体端口类型别名
using GPIOA = GPIO_Port<struct GPIOA_Tag>;
using GPIOB = GPIO_Port<struct GPIOB_Tag>;

} // namespace stm32f4

// 使用示例
void gpio_init() {
    using namespace stm32f4;

    // 配置PA5为输出（板载LED）
    GPIOA::config_mode<5>(GPIOA::Output);
    GPIOA::config_output<5>(GPIOA::PushPull);

    // 控制LED
    GPIOA::ODR odr{RegisterAddress<struct GPIOA_Tag>{0x40020014}};
    odr.set(1 << 5);    // 点亮
    odr.clear(1 << 5);  // 熄灭
}
```

### UART配置示例

```cpp
namespace stm32f4 {

template<typename Tag>
struct UART {
    using CR1 = Register<uint32_t, RegisterAddress<Tag>{0x0}>;
    using CR2 = Register<uint32_t, RegisterAddress<Tag>{0x4}>;
    using BRR = Register<uint32_t, RegisterAddress<Tag>{0x8}>;

    // 波特率计算（简化版）
    template<int ClockFreq, int BaudRate>
    static constexpr uint32_t calculate_brr() {
        return (ClockFreq + BaudRate / 2) / BaudRate;
    }

    // 编译期配置
    template<int ClockFreq, int BaudRate, bool EnableParity = false, bool Even = true>
    static void config() {
        constexpr uint32_t brr_value = calculate_brr<ClockFreq, BaudRate>();

        BRR brr{RegisterAddress<Tag>{0x40011000 + 0x8}};
        brr.write(brr_value);

        CR1 cr1{RegisterAddress<Tag>{0x40011000}};
        uint32_t cr1_value = (1 << 13);  // UE: UART使能
        cr1_value |= (1 << 2);   // RE: 接收使能
        cr1_value |= (1 << 3);   // TE: 发送使能

        if constexpr (EnableParity) {
            cr1_value |= (1 << 10);  // PCE: 奇偶校验使能
            if constexpr (!Even) {
                cr1_value |= (1 << 9);  // PS: 奇校验
            }
        }

        cr1.write(cr1_value);
    }
};

using UART1 = UART<struct UART1_Tag>;

} // namespace stm32f4

// 使用：在编译期配置UART
void uart_init() {
    // 假设APB2时钟84MHz，配置为115200波特率
    // 这些值都在编译期计算
    stm32f4::UART1::config<84000000, 115200>();
}
```

### 编译期引脚映射

```cpp
// 引脚映射配置
template<int Port, int Pin>
struct Pin {
    static constexpr int port = Port;
    static constexpr int pin = Pin;

    // 编译期生成引脚号
    static constexpr int pin_number = Port * 16 + Pin;

    // 编译期生成位掩码
    static constexpr uint32_t mask = 1 << Pin;
};

// 引脚别名
using PA5 = Pin<0, 5>;   // GPIOA, Pin 5
using PB13 = Pin<1, 13>; // GPIOB, Pin 13
using PC9 = Pin<2, 9>;   // GPIOC, Pin 9

// LED定义
using LED = PA5;

// 编译期检查
static_assert(LED::pin_number == 5);
static_assert(LED::mask == 0x20);

// 引脚功能配置
template<typename Pin, typename AlternateFunc>
struct AlternateFunction {
    static_assert(Pin::pin < 16, "Invalid pin number");

    // AFR寄存器：pin 0-7使用AFRL，pin 8-15使用AFRH
    static constexpr bool use_high = Pin::pin >= 8;
    static constexpr int reg_offset = use_high ? 4 : 0;
    static constexpr int shift = (Pin::pin % 8) * 4;
    static constexpr uint32_t mask = 0xF << shift;

    static constexpr uint32_t afr_value = static_cast<uint32_t>(AlternateFunc::value) << shift;
};

// SPI引脚配置示例
enum class SPI_AF : uint32_t {
    AF5 = 5,  // SPI1/2
    AF6 = 6   // SPI3
};

using SPI1_SCK = AlternateFunction<Pin<0, 5>, SPI_AF::AF5>;  // PA5 -> AF5
using SPI1_MISO = AlternateFunction<Pin<0, 6>, SPI_AF::AF5>; // PA6 -> AF5
using SPI1_MOSI = AlternateFunction<Pin<0, 7>, SPI_AF::AF5>; // PA7 -> AF5
```

### 完整示例：DMA配置

<details>
<summary>点击展开DMA编译期配置示例</summary>

```cpp
namespace stm32f4 {

// DMA通道定义
template<int Stream, int Channel>
struct DMA {
    static_assert(Stream >= 0 && Stream < 8, "Invalid DMA stream");
    static_assert(Channel >= 0 && Channel < 8, "Invalid DMA channel");

    static constexpr uintptr_t base = 0x40026000;
    static constexpr uintptr_t stream_base = base + Stream * 0x10 + 0x10 * (Stream / 4);

    using CR = Register<uint32_t, RegisterAddress<struct DMA_Tag>{stream_base + 0x00}>;
    using NDTR = Register<uint16_t, RegisterAddress<struct DMA_Tag>{stream_base + 0x04}>;
    using PAR = Register<uint32_t, RegisterAddress<struct DMA_Tag>{stream_base + 0x08}>;
    using M0AR = Register<uint32_t, RegisterAddress<struct DMA_Tag>{stream_base + 0x0C}>;
    using M1AR = Register<uint32_t, RegisterAddress<struct DMA_Tag>{stream_base + 0x10}>;

    // 方向
    enum Direction : uint32_t {
        PeripheralToMemory = 0,
        MemoryToPeripheral = 1,
        MemoryToMemory = 2
    };

    // 数据宽度
    enum Width : uint32_t {
        Byte = 0,
        HalfWord = 1,
        Word = 2
    };

    // 编译期配置
    template<
        Direction Dir,
        Width MemWidth = Word,
        Width PeriphWidth = Word,
        bool MemInc = true,
        bool PeriphInc = false,
        bool Circular = false
    >
    static void config() {
        constexpr uint32_t cr_value =
            (Channel << 25) |           // 通道选择
            (Dir << 6) |                // 方向
            (MemInc << 9) |             // 内存递增
            (PeriphInc << 8) |          // 外设递增
            (MemWidth << 10) |          // 内存宽度
            (PeriphWidth << 8) |        // 外设宽度
            (Circular << 5);            // 循环模式

        CR cr{RegisterAddress<struct DMA_Tag>{stream_base}};
        cr.write(cr_value);
    }

    // 启动传输
    static void enable() {
        CR cr{RegisterAddress<struct DMA_Tag>{stream_base}};
        cr.set(1 << 0);  // EN位
    }

    static void disable() {
        CR cr{RegisterAddress<struct DMA_Tag>{stream_base}};
        cr.clear(1 << 0);
    }
};

// 预定义DMA配置
using UART1_TX_DMA = DMA<4, 4>;  // Stream 4, Channel 4

void uart_dma_send_example() {
    uint8_t tx_buffer[256];
    uintptr_t uart1_dr = 0x40011004;

    // 配置DMA
    UART1_TX_DMA::config<
        DMA<4, 4>::MemoryToPeripheral,
        DMA<4, 4>::Byte,
        DMA<4, 4>::Byte,
        true, false
    >();

    // 设置地址
    UART1_TX_DMA::NDTR ndtr{RegisterAddress<struct DMA_Tag>{UART1_TX_DMA::stream_base + 0x04}};
    ndtr.write(256);

    UART1_TX_DMA::PAR par{RegisterAddress<struct DMA_Tag>{UART1_TX_DMA::stream_base + 0x08}};
    par.write(uart1_dr);

    UART1_TX_DMA::M0AR m0ar{RegisterAddress<struct DMA_Tag>{UART1_TX_DMA::stream_base + 0x0C}};
    m0ar.write(reinterpret_cast<uintptr_t>(tx_buffer));

    // 启动
    UART1_TX_DMA::enable();
}

} // namespace stm32f4

```

</details>

------

## 常见陷阱与解决方案

### 陷阱1：浮点类型作为非类型模板参数

**问题**：C++17之前不支持浮点类型的非类型模板参数：

```cpp
// C++17之前：错误
template<double Value>
struct DoubleConstant { };

// C++17之前的解决方案：转换为整数比例
template<int Numerator, int Denominator = 1>
struct Ratio {
    static constexpr double value = static_cast<double>(Numerator) / Denominator;
};

using PiApprox = Ratio<314159, 100000>;  // 3.14159
```

**C++20解决方案**：

```cpp
// C++20：直接支持浮点类型
template<double Value>
struct DoubleConstant {
    static constexpr double value = Value;
};

using Pi = DoubleConstant<3.14159265358979323846>;
```

### 陷阱2：类对象作为非类型模板参数

**C++20之前**：不能使用类对象作为非类型模板参数：

```cpp
// ❌ C++17错误
struct Config {
    int value;
    Config(int v) : value(v) {}
};

template<Config C>
struct ConfigHolder { };
```

**C++20解决方案**：需要类类型是"字面类型"：

```cpp
// ✅ C++20：字面类型
struct Config {
    int value;
    constexpr Config(int v) : value(v) {}
};

// constexpr变量可以作为模板参数
constexpr Config my_config{42};

template<Config C>
struct ConfigHolder {
    static constexpr int config_value = C.value;
};

using MyHolder = ConfigHolder<my_config>;
static_assert(MyHolder::config_value == 42);
```

### 陷阱3：不同模板参数类型导致重复实例化

```cpp
template<int N>
struct Buffer {
    uint8_t data[N];
};

Buffer<10> b1;    // 实例化1
Buffer<010> b2;   // 实例化2（八进制10 = 十进制8）！
Buffer<0xA> b3;   // 错误：非整型字面量
Buffer<10> b4;    // 与b1共享实例

// 解决方案：使用强类型包装
template<std::size_t N>
struct Size {
    static constexpr std::size_t value = N;
};

template<typename S>
struct TypedBuffer {
    uint8_t data[S::value];
};

using Size10 = Size<10>;
TypedBuffer<Size10> tb1;
TypedBuffer<Size<10>> tb2;  // 同一类型
```

### 陷阱4：编译期常量与运行时值的混淆

```cpp
template<std::size_t N>
void process(int (&arr)[N]) {
    // N是编译期常量
    std::array<int, N> arr_copy;  // OK

    std::size_t runtime_size = get_size();
    // std::array<int, runtime_size> arr2;  // 错误：需要编译期常量
}

// 解决方案：使用constexpr函数返回编译期值
constexpr std::size_t get_buffer_size() {
    return 256;
}

std::array<int, get_buffer_size()> buffer;  // OK
```

### 陷阱5：链接性问题

```cpp
// 头文件 A.h
template<const char* Str>
struct A { };

// 源文件 a.cpp
const char hello[] = "hello";

// 头文件 B.h
template<const char* Str>
struct B { };

// 问题：字符串字面量具有内部链接
// template<const char* Str> struct C { };
// C<"hello"> c;  // 错误：某些编译器不允许

// 解决方案：使用extern
extern constexpr char world[] = "world";
A<world> a;  // OK
```

### 陷阱6：模板参数依赖的名称查找

```cpp
template<int N>
struct Factorial {
    static constexpr int value = N * Factorial<N-1>::value;
};

template<>
struct Factorial<0> {
    static constexpr int value = 1;
};

// 问题：某些编译器需要显式指定模板参数
template<int N>
struct Factorial2 {
    static constexpr int value = N * ::Factorial2<N-1>::value;  // 加::限定
};

// 更好的解决方案：使用别名模板
template<int N>
using Factor = Factorial<N>;

constexpr int result = Factor<5>::value;
```

------

## C++20新特性

### 浮点非类型模板参数

```cpp
template<double Value>
struct DoubleParam {
    static constexpr double value = Value;
    constexpr double operator()() const { return Value; }
};

// 使用
constexpr DoubleParam<3.14159> pi;
constexpr auto pi_value = pi();
static_assert(pi_value > 3.14 && pi_value < 3.15);

// 应用：编译期三角函数
template<double Angle>
struct SinTable {
    static constexpr double value = std::sin(Angle);
};

constexpr auto sin_30 = SinTable<3.14159265358979323846 / 6>::value;
static_assert(sin_30 > 0.49 && sin_30 < 0.51);
```

### 字面类型非类型模板参数

```cpp
struct Point {
    int x, y;
    constexpr Point(int x_, int y_) : x(x_), y(y_) {}
    constexpr bool operator==(const Point& other) const {
        return x == other.x && y == other.y;
    }
};

template<Point P>
struct PointHolder {
    static constexpr int x = P.x;
    static constexpr int y = P.y;
};

// 使用
constexpr Point origin{0, 0};
using Origin = PointHolder<origin>;

static_assert(Origin::x == 0);
static_assert(Origin::y == 0);
```

### 非类型模板参数的类模板参数推导（CTAD）

```cpp
template<auto Value>
struct ValueWrapper {
    constexpr static auto value = Value;
};

// CTAD
ValueWrapper w1{42};        // 推导为ValueWrapper<int, 42>
ValueWrapper w2{3.14};      // 推导为ValueWrapper<double, 3.14>
ValueWrapper w3{'a'};       // 推导为ValueWrapper<char, 'a'>
```

### Concepts约束非类型模板参数

```cpp
#include <concepts>

template<std::integral auto N>
struct IntegralBuffer {
    static constexpr auto size = N;
    int data[N];
};

template<std::floating_point auto F>
struct FloatingWrapper {
    static constexpr auto value = F;
};

IntegralBuffer<10> ib;       // OK
// IntegralBuffer<3.14> ib2;   // 错误：不是整数类型

FloatingWrapper<2.718> fw;   // OK
// FloatingWrapper<42> fw2;    // 错误：不是浮点类型
```

### 三路比较支持

```cpp
template<auto Value>
requires requires { { Value } <=> std::declval<decltype(Value)>(); }
struct OrderedValue {
    constexpr static auto value = Value;

    constexpr auto operator<=>(const OrderedValue&) const = default;
};

OrderedValue<10> v1;
OrderedValue<20> v2;
static_assert(v1 < v2);
```

------

## 小结

非类型模板参数是C++模板系统中强大而独特的特性，特别适合嵌入式开发：

### 关键要点总结

| 特性 | 说明 | 嵌入式应用 |
|------|------|----------|
| 整型参数 | 编译期常量整数 | 数组大小、位掩码、波特率 |
| 指针参数 | 编译期绑定的地址 | 寄存器映射、内存地址 |
| 引用参数 | 编译期绑定的引用 | 全局配置、设备描述 |
| auto参数（C++17） | 自动推导类型 | 通用值包装器 |
| 浮点参数（C++20） | 编译期浮点常量 | 三角函数表、物理常数 |

### 实战建议

1. **类型安全优先**
   - 使用强类型包装代替宏
   - 使用`static_assert`进行编译期验证
   - 使用标签类型防止类型混淆

2. **编译期计算**
   - 将运行时计算移到编译期
   - 使用`constexpr`函数生成模板参数
   - 利用编译期常量进行优化

3. **代码组织**
   - 将相关非类型模板参数组织成配置结构
   - 使用别名模板简化复杂类型
   - 为常用配置创建预定义类型

4. **性能考虑**
   - 非类型模板参数不会增加运行时开销
   - 但过度使用会导致代码膨胀
   - 合理共享模板实例

### 检查清单

使用非类型模板参数时，确保：

- [ ] 参数类型是编译期可确定的
- [ ] 指针/引用参数指向外部链接对象
- [ ] 数组大小在合理范围内
- [ ] 添加适当的`static_assert`检查
- [ ] 为常用配置提供类型别名
- [ ] 文档说明参数的有效范围

**下一章**，我们将探讨**类模板**，学习如何实现泛型容器、理解模板成员函数的特殊规则，并深入探索模板特化与偏特化的高级应用。
