---
title: "模板别名与Using声明"
description: "掌握现代C++类型别名的强大功能与标准库实现原理"
chapter: 12
order: 7
tags:
  - cpp-modern
  - host
  - intermediate
difficulty: intermediate
reading_time_minutes: 25
prerequisites:
  - "Chapter 12: 类模板详解"
  - "Chapter 12: 模板元编程基础"
cpp_standard: [11, 14, 17, 20]
platform: host
---

# 嵌入式现代C++教程——模板别名与Using声明

你有没有被这种代码折磨过？

```cpp
std::map<std::string, std::shared_ptr<Sensor>>::iterator it = sensors.begin();

// 或者更可怕的
typename std::enable_if<std::is_integral<T>::value, T>::type result = process(value);
```

这种代码不仅难读，还容易出错。C++11引入的`using`声明和别名模板正是解决这个问题的利器。本章我们将深入探讨类型别名的现代写法，追溯标准库中`conditional_t`、`enable_if_t`的设计原理，并学习如何用别名模板简化复杂的模板类型声明。

------

## 从 typedef 到 using

### typedef 的前世今生

在C++11之前，我们只能用`typedef`来定义类型别名：

```cpp
// 基本用法
typedef unsigned int uint32_t;
typedef int int_array[10];
typedef void (*function_ptr)(int);

// 复杂一点的
typedef std::map<std::string, std::vector<int>> StringToIntVectorMap;
```

`typedef`的问题很明显：

1. **语法不一致**：`typedef`声明的语法与变量声明类似，但关键字位置别扭
2. **不支持模板**：无法为模板类型定义别名
3. **可读性差**：复杂类型别名难以解析

```cpp
// 这是什么？
typedef void (*SignalHandler)(int, siginfo_t*, void*);

// 需要从右向左读：SignalHandler是一个函数指针类型，
// 指向的函数接受(int, siginfo_t*, void*)参数，返回void
```

### using 的优雅登场

C++11引入的`using`声明提供了更清晰的语法：

```cpp
// 基本用法
using uint32_t = unsigned int;
using int_array = int[10];
using function_ptr = void(*)(int);

// 语法更自然：从左向右读
using SignalHandler = void(*)(int, siginfo_t*, void*);
```

**关键优势**：`using`语法的"别名 = 原类型"结构更符合直觉，与变量初始化的写法一致。

### typedef vs using 对比表

| 特性 | typedef | using |
|------|---------|-------|
| 基本类型别名 | 支持 | 支持 |
| 函数指针别名 | 支持 | 支持（更清晰） |
| 模板别名 | 不支持 | 支持 |
| 模板模板参数 | 困难 | 直观 |
| 作用域 | 同 | 同 |
| 可读性 | 较差 | 更好 |

### 函数指针别名对比

```cpp
// typedef 写法
typedef void (*OldStyleCallback)(int error_code, const char* message);
OldStyleCallback cb1 = nullptr;

// using 写法
using NewStyleCallback = void(*)(int error_code, const char* message);
NewStyleCallback cb2 = nullptr;

// 多个参数的函数指针
typedef void (*ComplexOld)(int, double, const std::string&);
using ComplexNew = void(*)(int, double, const std::string&);

// 返回函数指针的函数
// typedef 版本（非常晦涩）
typedef void (*FuncPtr)(int);
FuncPtr getFunc typedef_Old(int id);

// using 版本（相对清晰）
using FuncPtr = void(*)(int);
FuncPtr getFunc_New(int id);

// 返回函数指针的函数指针...你能看出区别吗？
using CallbackFactory = auto(*)(int) -> void(*)(int);
```

------

## 别名模板（Alias Templates）

### typedef 的致命缺陷

`typedef`最大的问题是**无法为模板定义别名**：

```cpp
template<typename T>
struct MyAllocator {
    using value_type = T;
    T* allocate(std::size_t n);
    void deallocate(T* p, std::size_t n);
};

// ❌ typedef 无法做到
// typedef MyAllocator<T> MyAlloc;  // 错误：T 未定义

// 只能为具体类型别名
typedef MyAllocator<int> MyIntAllocator;  // 只适用于 int
```

这导致大量重复代码，每次使用不同类型都要重新声明。

### using 别名模板

C++11的`using`完美解决了这个问题：

```cpp
// ✅ 别名模板
template<typename T>
using MyAlloc = MyAllocator<T>;

// 使用
MyAlloc<int> int_alloc;
MyAlloc<double> double_alloc;
MyAlloc<std::string> string_alloc;
```

**关键点**：别名模板**不是新类型的定义**，而是现有类型的语法糖。模板实例化时会直接替换为原类型。

### 实际应用：简化标准库容器

```cpp
// 嵌入式常用配置
template<typename T, std::size_t N>
using FixedVector = std::vector<T, CustomAllocator<T, N>>;

// 使用
FixedVector<uint8_t, 32> rx_buffer;  // 32字节接收缓冲区
FixedVector<int16_t, 16> adc_samples; // 16个ADC采样

// 更复杂的例子
template<typename Key, typename Value>
using StringMap = std::map<Key, Value, std::less<Key>, PoolAllocator<64>>;

StringMap<std::string, int> settings;
```

### 别名模板 vs 特化

别名模板**不能被特化**，这是它与类模板的重要区别：

```cpp
// 别名模板（不能特化）
template<typename T>
using Ptr = T*;

// ❌ 错误：别名模板不能特化
// template<>
// using Ptr<void> = void*;

// 如果需要特化，使用类模板
template<typename T>
struct PtrTraits {
    using type = T*;
};

// 可以特化
template<>
struct PtrTraits<void> {
    using type = void*;
};

// 使用
template<typename T>
using SafePtr = typename PtrTraits<T>::type;
```

### 嵌入式场景：外设类型别名

```cpp
// HAL 层类型定义
template<typename RegType, RegType Address>
struct Register {
    static constexpr RegType address = Address;
    using value_type = RegType;

    RegType read() const;
    void write(RegType value);
};

// 为常用寄存器创建别名
using GPIOA_MODER = Register<uint32_t, 0x48000000>;
using GPIOA_ODR = Register<uint32_t, 0x48000014>;
using SPI1_DR = Register<uint16_t, 0x4001300C>;

// 使用
void init_gpio() {
    GPIOA_MODER moder;
    moder.write(0xABCDFFFF);
}
```

------

## 标准库溯源：_t 后缀的由来

你一定见过很多`_t`后缀的类型别名：

```cpp
std::size_t
std::int32_t
std::make_signed_t<int>
std::enable_if_t<condition, T>
std::conditional_t<flag, T, U>
```

这些其实是**别名模板**的命名约定——`_t`表示"类型"（type）。让我们追溯它们的设计原理。

### std::enable_if_t 的演变

#### C++11/14 原始版本

```cpp
// 标准库实现
template<bool Condition, typename T = void>
struct enable_if {
    // 空：当 Condition 为 false 时
};

template<typename T>
struct enable_if<true, T> {
    using type = T;  // 只有 Condition 为 true 时才有 type 成员
};

// 使用方式（冗长）
template<typename T>
typename std::enable_if<std::is_integral<T>::value, T>::type
process(T value) {
    return value * 2;
}
```

#### C++14 别名模板简化

```cpp
// C++14 添加的别名模板
template<bool B, typename T = void>
using enable_if_t = typename enable_if<B, T>::type;

// 使用方式（简洁！）
template<typename T>
std::enable_if_t<std::is_integral_v<T>, T>
process(T value) {
    return value * 2;
}
```

#### 实现原理剖析

```cpp
// 逐步剖析
template<bool B, typename T = void>
using enable_if_t = typename enable_if<B, T>::type;
//                        ^^^^^^^^^^^^^^^^^^^^^^^^^^
//                        这部分引用原始 struct 的 type 成员

// 当 B = true 时
// enable_if<true, int>::type = int
// 所以 enable_if_t<true, int> = int

// 当 B = false 时
// enable_if<false, int> 没有 type 成员
// 所以 enable_if_t<false, int> 替换失败，触发 SFINAE
```

### std::conditional_t 的设计

#### C++11/14 原始版本

```cpp
// 标准库实现
template<bool Condition, typename TrueType, typename FalseType>
struct conditional {
    using type = TrueType;  // Condition 为 true 时的默认情况
};

template<typename TrueType, typename FalseType>
struct conditional<false, TrueType, FalseType> {
    using type = FalseType;  // Condition 为 false 时的特化
};

// 使用方式（冗长）
template<typename T>
void process(typename std::conditional<
    std::is_integral<T>::value,
    std::integral_constant<char, 'I'>,
    std::integral_constant<char, 'F'>
>::type tag);
```

#### C++14 别名模板简化

```cpp
// C++14 添加的别名模板
template<bool B, typename T, typename F>
using conditional_t = typename conditional<B, T, F>::type;

// 使用方式（简洁！）
template<typename T>
void process(std::conditional_t<
    std::is_integral_v<T>,
    std::integral_constant<char, 'I'>,
    std::integral_constant<char, 'F'>
> tag);
```

#### 实际应用示例

```cpp
// 根据类型大小选择最优算法
template<typename T>
using FastCopy = std::conditional_t<
    sizeof(T) == 1,                    // 如果是 1 字节
    void(*)(uint8_t*, const uint8_t*, std::size_t),  // 使用 memcpy
    void(*)(T*, const T*, std::size_t)               // 否则使用循环
>;

// 嵌入式场景：根据架构选择指令
template<typename T>
using OptimalAdd = std::conditional_t<
    std::is_integral_v<T> && sizeof(T) <= 4,
    std::integral_constant<int, 1>,    // 使用单周期加法
    std::integral_constant<int, 2>     // 需要多周期或库调用
>;
```

### 常用的 _t 别名模板

| 别名模板 | 说明 | 示例 |
|---------|------|------|
| `enable_if_t<B, T>` | 条件启用类型 | `std::enable_if_t<std::is_integral_v<T>>` |
| `conditional_t<B, T, F>` | 条件选择类型 | `std::conditional_t<flag, int, double>` |
| `remove_reference_t<T>` | 移除引用 | `std::remove_reference_t<int&>` = `int` |
| `add_const_t<T>` | 添加 const | `std::add_const_t<int>` = `const int` |
| `make_signed_t<T>` | 转为有符号 | `std::make_signed_t<unsigned int>` = `int` |
| `make_unsigned_t<T>` | 转为无符号 | `std::make_unsigned_t<int>` = `unsigned int` |
| `common_type_t<T, U>` | 公共类型 | `std::common_type_t<int, double>` = `double` |
| `decay_t<T>` | 衰退类型 | `std::decay_t<int[5]>` = `int*` |
| `invoke_result_t<F, Args>` | 调用结果类型 | `std::invoke_result_t<decltype(func), int>` |

------

## 实战：简化复杂模板类型声明

让我们通过几个实际场景展示别名模板的威力。

### 场景1：类型安全的通信协议

```cpp
// 协议基础类型
template<typename T>
struct BigEndian {
    T value;
    BigEndian() = default;
    explicit BigEndian(T v) : value(htobe<T>(v)) {}
    T get() const { return betoh<T>(value); }
};

// 为常用大小创建别名
using BE16 = BigEndian<uint16_t>;
using BE32 = BigEndian<uint32_t>;
using BE64 = BigEndian<uint64_t>;

// 协议头部
struct PacketHeader {
    BE16 sync;        // 同步字
    BE16 length;      // 长度
    BE32 timestamp;   // 时间戳
    BE16 checksum;    // 校验和
};

// 解析函数
void parse_packet(const uint8_t* buffer) {
    auto header = reinterpret_cast<const PacketHeader*>(buffer);
    uint16_t len = header->length.get();  // 自动转换字节序
    // ...
}
```

### 场景2：嵌入式回调系统

```cpp
// 回调类型定义
template<typename... Args>
using Callback = void(*)(Args...);

// 具体回调别名
using ErrorHandler = Callback<int, const char*>;
using DataHandler = Callback<const uint8_t*, std::size_t>;
using TimeoutHandler = Callback<void>;

// 回调管理器
template<typename... Args>
class CallbackManager {
public:
    using CallbackType = Callback<Args...>;
    using Token = std::size_t;

    Token register_callback(CallbackType cb) {
        Token token = next_token_++;
        callbacks_[token] = cb;
        return token;
    }

    void invoke(Args... args) {
        for (auto& [token, cb] : callbacks_) {
            if (cb) cb(args...);
        }
    }

private:
    std::map<Token, CallbackType> callbacks_;
    Token next_token_ = 0;
};

// 使用
void error_callback(int code, const char* msg) {
    log_error("Error %d: %s", code, msg);
}

// 注册
CallbackManager<int, const char*> error_manager;
auto token = error_manager.register_callback(error_callback);
```

### 场景3：内存池配置

```cpp
// 内存池特征
template<std::size_t BlockSize, std::size_t Blocks>
struct PoolTraits {
    static constexpr std::size_t block_size = BlockSize;
    static constexpr std::size_t blocks = Blocks;
    static constexpr std::size_t total_size = BlockSize * Blocks;
    using block_type = std::aligned_storage_t<BlockSize>;
};

// 常用配置
using SmallPool = PoolTraits<32, 64>;      // 2KB
using MediumPool = PoolTraits<128, 32>;    // 4KB
using LargePool = PoolTraits<512, 16>;     // 8KB

// 内存池实现
template<typename Traits>
class MemoryPool {
public:
    using BlockType = typename Traits::block_type;

    void* allocate() {
        if (free_list_) {
            auto block = free_list_;
            free_list_ = *static_cast<BlockType**>(block);
            return block;
        }
        return nullptr;
    }

    void deallocate(void* ptr) {
        auto block = static_cast<BlockType**>(ptr);
        *block = free_list_;
        free_list_ = block;
    }

private:
    BlockType storage_[Traits::blocks];
    void* free_list_ = nullptr;
};

// 使用
MemoryPool<SmallPool> small_objects;
MemoryPool<MediumPool> medium_objects;
```

### 场景4：智能指针类型别名

```cpp
// 项目统一的智能指针配置
template<typename T>
using UniquePtr = std::unique_ptr<T, CustomDeleter<T>>;

template<typename T>
using SharedPtr = std::shared_ptr<T>;

// 线程安全的智能指针
template<typename T>
using ThreadSafePtr = std::shared_ptr<T>;

// 单例智能指针
template<typename T>
using SingletonPtr = std::unique_ptr<T>;

// 外设指针（硬件寄存器）
template<typename T, T Address>
using PeripheralPtr = std::uintptr_t;

// 使用
class UARTDriver {
public:
    static UniquePtr<UARTDriver> instance() {
        return UniquePtr<UARTDriver>(new UARTDriver());
    }
};

auto uart = UARTDriver::instance();
```

### 场景5：传感器接口类型

```cpp
// 传感器读取结果
template<typename T>
struct SensorResult {
    T value;
    bool valid;
    uint32_t timestamp;
};

// 常用传感器结果类型
using TemperatureResult = SensorResult<float>;
using PressureResult = SensorResult<double>;
using AccelerationResult = SensorResult<std::array<float, 3>>;

// 传感器接口
template<typename ResultType>
class ISensor {
public:
    using Result = ResultType;

    virtual Result read() = 0;
    virtual bool calibrate() = 0;
};

// 具体传感器
class TempSensor : public ISensor<TemperatureResult> {
public:
    Result read() override {
        Result r;
        r.value = read_temperature();
        r.valid = true;
        r.timestamp = get_timestamp();
        return r;
    }
};
```

### 场景6：类型特征组合

```cpp
// 检测类型是否适合作为 DMA 缓冲区
template<typename T>
using is_dma_capable = std::conjunction<
    std::is_trivially_copyable<T>,
    std::has_unique_object_representations<T>,
    std::bool_constant<(alignof(T) >= 4)>
>;

// 获取最佳 DMA 传输类型
template<typename T>
using dma_transfer_type = std::conditional_t<
    is_dma_capable<T>::value,
    T,                    // 直接传输
    std::aligned_storage_t<sizeof(T), alignof(T)>  // 需要拷贝
>;

// 使用示例
template<typename T>
void dma_transfer(const T* src, T* dst, std::size_t count) {
    using TransferType = dma_transfer_type<T>;

    if constexpr (is_dma_capable<T>::value) {
        // 直接 DMA 传输
        start_dma(src, dst, count * sizeof(T));
    } else {
        // 需要先对齐
        std::array<TransferType, 256> buffer;
        // 拷贝到缓冲区，然后 DMA
    }
}
```

------

## 高级技巧：编译期类型选择

### 技巧1：递归类型构建

```cpp
// 构建固定大小的整数类型
template<std::size_t Bits>
struct IntOfSize {
    static_assert(Bits <= 64, "Type too large");
    using type = std::conditional_t<
        Bits <= 8, uint8_t,
        std::conditional_t<
            Bits <= 16, uint16_t,
            std::conditional_t<
                Bits <= 32, uint32_t,
                uint64_t
            >
        >
    >;
};

template<std::size_t Bits>
using IntOfSize_t = typename IntOfSize<Bits>::type;

// 使用
IntOfSize_t<12> value = 0xFFF;  // 使用 uint16_t
IntOfSize_t<24> data = 0xFFFFFF; // 使用 uint32_t
```

### 技巧2：SFINAE 友好的类型检测

```cpp
// 检测类型是否有 serialize 方法
template<typename, typename = void>
struct has_serialize : std::false_type {};

template<typename T>
struct has_serialize<T, std::void_t<decltype(std::declval<T>().serialize())>> : std::true_type {};

template<typename T>
using has_serialize_t = typename has_serialize<T>::type;

template<typename T>
constexpr bool has_serialize_v = has_serialize<T>::value;

// 使用
template<typename T>
std::enable_if_t<has_serialize_v<T>, std::string>
to_string(const T& obj) {
    return obj.serialize();
}

template<typename T>
std::enable_if_t<!has_serialize_v<T>, std::string>
to_string(const T& obj) {
    return std::to_string(obj);
}
```

### 技巧3：策略选择别名

```cpp
// 根据类型选择最优策略
template<typename T>
using OptimizedCopy = std::conditional_t<
    std::is_trivially_copyable_v<T>,
    std::integral_constant<int, 0>,  // memcpy
    std::conditional_t<
        std::is_pointer_v<T>,
        std::integral_constant<int, 1>,  // 指针拷贝
        std::integral_constant<int, 2>   // 逐元素拷贝
    >
>;

// 策略实现
template<typename T>
void copy_optimized(T* dst, const T* src, std::size_t n) {
    constexpr auto strategy = OptimizedCopy<T>::value;

    if constexpr (strategy == 0) {
        std::memcpy(dst, src, n * sizeof(T));
    } else if constexpr (strategy == 1) {
        for (std::size_t i = 0; i < n; ++i) {
            dst[i] = src[i];
        }
    } else {
        std::copy_n(src, n, dst);
    }
}
```

### 技巧4：延迟类型计算

```cpp
// 延迟计算类型（避免实例化不完整的类型）
template<typename T>
struct LazyType {
    using type = T;
};

template<typename T>
using LazyType_t = typename LazyType<T>::type;

// 使用场景：前向声明
template<typename T>
class Container {
    using iterator = LazyType_t<typename std::conditional<
        std::is_const_v<T>,
        const T*,
        T*
    >::type>;
};
```

------

## 嵌入式最佳实践

### 实践1：硬件寄存器类型别名

```cpp
// 寄存器宽度定义
using Reg8 = volatile uint8_t;
using Reg16 = volatile uint16_t;
using Reg32 = volatile uint32_t;

// 位域定义
template<typename Reg, typename BitType>
struct BitField {
    Reg& reg;
    static constexpr BitType mask = BitType::mask;
    static constexpr uint8_t offset = BitType::offset;

    void set() { reg |= mask; }
    void clear() { reg &= ~mask; }
    bool is_set() const { return reg & mask; }
    BitType get() const { return static_cast<BitType>((reg >> offset) & mask); }
};

// 使用
using GPIO_PIN5 = BitField<Reg32, struct { static constexpr uint32_t mask = 1 << 5; static constexpr uint8_t offset = 5; }>;
```

### 实践2：编译期缓冲区配置

```cpp
// 缓冲区配置
template<std::size_t Size>
struct BufferConfig {
    static constexpr std::size_t size = Size;
    static constexpr std::size_t alignment = (Size >= 64) ? 8 : 4;
    using storage_type = std::aligned_storage_t<Size, alignment>;
};

// 常用配置
using RxBufferConfig = BufferConfig<256>;
using TxBufferConfig = BufferConfig<512>;
using LogBufferConfig = BufferConfig<1024>;

// 缓冲区实现
template<typename Config>
class Buffer {
    alignas(Config::alignment) typename Config::storage_type storage_;
public:
    // ...
};
```

### 实践3：DMA 描述符类型

```cpp
// DMA 描述符
struct DMADescriptor {
    std::uintptr_t src_addr;
    std::uintptr_t dst_addr;
    std::uint32_t control;
    std::uint32_t reserved;
};

// DMA 通道类型别名
using DMADescriptorArray = std::array<DMADescriptor, 8>;
using DMADescriptorPtr = std::uintptr_t;  // 物理地址
```

### 实践4：错误码类型安全

```cpp
// 错误码基类
template<typename Tag, typename Underlying = int>
class ErrorCode {
public:
    using value_type = Underlying;

    constexpr ErrorCode() : value_(0) {}
    constexpr explicit ErrorCode(value_type v) : value_(v) {}

    constexpr value_type value() const { return value_; }
    constexpr bool operator==(const ErrorCode& other) const { return value_ == other.value_; }
    constexpr bool operator!=(const ErrorCode& other) const { return value_ != other.value_; }

private:
    value_type value_;
};

// 具体错误码类型
using IOError = ErrorCode<struct IOTag>;
using SPIError = ErrorCode<struct SPITag>;
using I2CError = ErrorCode<struct I2CTag>;

// 使用
I2CError i2c_write(uint8_t addr, const uint8_t* data, std::size_t len);
```

------

## 常见陷阱

### 陷阱1：别名模板不是类型

```cpp
template<typename T>
using Ptr = T*;

// ❌ 错误：不能前向声明别名模板
// template<typename T>
// using Ptr;

// ✅ 正确：直接使用
Ptr<int> p;

// ❌ 错误：不能特化别名模板
// template<>
// using Ptr<int> = void*;
```

### 陷阱2：依赖类型的 typename

```cpp
template<typename T>
class Container {
    // ❌ 错误：需要 typename
    // using ValueType = T::value_type;

    // ✅ 正确
    using ValueType = typename T::value_type;

    // C++20 可以省略（如果确定是类型）
    // using ValueType = T::value_type;
};
```

### 陷阱3：别名与 typedef 的可见性

```cpp
class Base {
protected:
    using value_type = int;
};

class Derived : public Base {
    // typedef 的行为不同
    // typedef Base::value_type value_type;  // 需要显式

    // using 可以直接引入
    using Base::value_type;  // OK
};
```

### 陷阱4：函数类型别名

```cpp
// ❌ 混淆的写法
// using Func = void();  // 这是函数类型，不是函数指针

// ✅ 正确的函数指针别名
using FuncPtr = void(*)();

// 函数类型（用于模板参数）
template<typename F>
void call(F f) { f(); }

// 使用
void my_func() {}
call(my_func);  // OK，函数退化为指针
call<FuncPtr>(my_func);  // 也可以
```

### 陷阱5：模板模板参数

```cpp
template<typename T>
using MyVector = std::vector<T, CustomAllocator<T>>;

// ❌ 错误：MyVector 不是模板模板参数
// template<template<typename> class Container>
// void process(Container<int> c);

// ✅ 正确：使用实际的模板
template<template<typename, typename> class Container>
void process(Container<int, CustomAllocator<int>> c);
```

------

## C++14/17/20 新特性

### C++14：更多 _t 别名

```cpp
// C++11/14 标准
using enable_if_t = typename enable_if<...>::type;
using conditional_t = typename conditional<...>::type;
using remove_reference_t = typename remove_reference<...>::type;

// 可以自己定义
template<typename T>
using my_type_t = typename MyTrait<T>::type;
```

### C++14：变量模板与别名

```cpp
// 变量模板
template<typename T>
constexpr bool is_integral_v = std::is_integral<T>::value;

// 配合别名模板使用
template<typename T>
using enable_if_integral = std::enable_if_t<is_integral_v<T>>;

// 使用
template<typename T, typename = enable_if_integral<T>>
void process(T t);
```

### C++17：std::void_t

```cpp
// C++17 标准
template<typename... Ts>
using void_t = void;

// SFINAE 检测
template<typename, typename = void>
struct has_member : std::false_type {};

template<typename T>
struct has_member<T, std::void_t<decltype(T::member)>> : std::true_type {};
```

### C++20：Concepts 与别名

```cpp
// Concept 定义
template<typename T>
concept Integral = std::is_integral_v<T>;

// 与别名模板配合
template<typename T>
using EnableIfIntegral = std::enable_if_t<Integral<T>>;

// 或者直接用 Concepts
template<Integral T>
void process(T t);

// 缩写函数模板
void process2(Integral auto t);
```

### C++20：requires 表达式

```cpp
template<typename T>
concept Iterable = requires(T t) {
    { t.begin() } -> std::same_as<typename T::iterator>;
    { t.end() } -> std::same_as<typename T::iterator>;
};

// 使用
template<Iterable T>
void process(T container);
```

------

## 实战：构建完整的类型系统

让我们用别名模板构建一个嵌入式项目中常用的类型系统。

```cpp
<details>
<summary>点击展开完整代码</summary>

```cpp
#include <cstdint>
#include <type_traits>
#include <array>
#include <memory>

// ==================== 基础类型别名 ====================

// 寄存器类型
using Reg8 = std::uint8_t;
using Reg16 = std::uint16_t;
using Reg32 = std::uint32_t;

// 大小类型
using SizeType = std::size_t;

// 时间戳类型
using Timestamp = std::uint32_t;

// ==================== 类型特征别名 ====================

// 检测是否为 POD 类型
template<typename T>
using IsPOD = std::is_pod<T>;

template<typename T>
constexpr bool IsPOD_v = IsPOD<T>::value;

// 检测是否为标准布局
template<typename T>
using IsStandardLayout = std::is_standard_layout<T>;

template<typename T>
constexpr bool IsStandardLayout_v = IsStandardLayout<T>::value;

// ==================== 条件类型选择 ====================

// DMA 传输类型
template<typename T>
using DMATransferType = std::conditional_t<
    IsPOD_v<T> && sizeof(T) <= 4,
    T,
    std::aligned_storage_t<sizeof(T), alignof(T)>
>;

// 中断处理函数类型
template<typename... Args>
using IRQHandler = void(*)(Args...);

// ==================== 容器类型别名 ====================

// 固定大小数组
template<typename T, std::size_t N>
using FixedArray = std::array<T, N>;

// 常用缓冲区大小
template<typename T>
using RxBuffer = FixedArray<T, 256>;

template<typename T>
using TxBuffer = FixedArray<T, 256>;

template<typename T>
using LogBuffer = FixedArray<T, 128>;

// ==================== 智能指针别名 ====================

// 项目统一的智能指针配置
template<typename T>
using UniquePtr = std::unique_ptr<T>;

template<typename T>
using SharedPtr = std::shared_ptr<T>;

// 单例指针
template<typename T>
using SingletonPtr = std::unique_ptr<T>;

// ==================== 错误处理类型 ====================

// 错误码基类
template<typename Tag, typename Underlying = int>
class ErrorCode {
public:
    using value_type = Underlying;

    constexpr ErrorCode() : value_(0) {}
    constexpr explicit ErrorCode(value_type v) : value_(v) {}

    constexpr value_type value() const { return value_; }

private:
    value_type value_;
};

// 具体错误码类型
using IOError = ErrorCode<struct IO_Tag>;
using SPIError = ErrorCode<struct SPI_Tag>;
using I2CError = ErrorCode<struct I2C_Tag>;
using ADCError = ErrorCode<struct ADC_Tag>;

// ==================== 传感器类型 ====================

// 传感器结果
template<typename T>
struct SensorResult {
    T value;
    bool valid;
    Timestamp timestamp;
};

// 常用传感器结果类型
using TemperatureResult = SensorResult<float>;
using PressureResult = SensorResult<double>;
using HumidityResult = SensorResult<float>;
using AccelerationResult = SensorResult<std::array<float, 3>>;

// 传感器接口
template<typename ResultType>
class ISensor {
public:
    using Result = ResultType;

    virtual ~ISensor() = default;
    virtual Result read() = 0;
    virtual bool calibrate() = 0;
};

// ==================== 回调类型 ====================

// 错误回调
template<typename... Args>
using ErrorCallback = void(*)(Args...);

using SystemErrorCallback = ErrorCallback<int, const char*>;

// 数据回调
template<typename T>
using DataCallback = void(*)(const T&, Timestamp);

// 完成回调
using CompletionCallback = void(*)(bool success);

// ==================== 配置类型 ====================

// UART 配置
struct UARTConfig {
    std::uint32_t baudrate;
    std::uint8_t data_bits;
    std::uint8_t stop_bits;
    bool parity;
};

// SPI 配置
struct SPIConfig {
    std::uint32_t baudrate;
    std::uint8_t mode;
    bool lsb_first;
};

// I2C 配置
struct I2CConfig {
    std::uint32_t clock_speed;
    std::uint16_t timeout_ms;
};

// ==================== 使用示例 ====================

class TemperatureSensor : public ISensor<TemperatureResult> {
public:
    Result read() override {
        Result r;
        r.value = 25.5f;
        r.valid = true;
        r.timestamp = 12345;
        return r;
    }

    bool calibrate() override {
        return true;
    }
};

void example_usage() {
    // 使用智能指针
    auto sensor = UniquePtr<ISensor<TemperatureResult>>(
        new TemperatureSensor()
    );

    // 读取传感器
    auto result = sensor->read();

    // 使用缓冲区
    RxBuffer<std::uint8_t> rx_data;
    TxBuffer<std::uint8_t> tx_data;

    // 使用错误码
    I2CError error = I2CError(1);

    // 使用回调
    SystemErrorCallback error_cb = [](int code, const char* msg) {
        // 处理错误
    };
}
```

</details>
```

------

## 性能分析

### 编译期 vs 运行期

| 特性 | 编译期开销 | 运行期开销 |
|------|-----------|-----------|
| typedef | 无 | 无 |
| using 别名 | 无 | 无 |
| 别名模板 | 实例化时间 | 无 |
| _t 后缀类型 | 实例化时间 | 无 |

**结论**：别名模板**没有运行时开销**，所有计算都在编译期完成。

### 代码大小

```cpp
// 测试代码大小
template<typename T>
using Ptr = T*;

Ptr<int> p1;
Ptr<double> p2;

// 生成的代码与以下完全相同：
int* p1;
double* p2;

// 别名模板只是语法糖，不产生额外代码
```

### 编译时间影响

```cpp
// 简单别名：编译时间可忽略
using MyInt = int;

// 别名模板：轻微增加编译时间
template<typename T>
using MyVector = std::vector<T>;

// 复杂的类型特征：可能显著增加编译时间
template<typename T>
using ComplexType = std::conditional_t<
    std::conjunction_v<A<T>, B<T>, C<T>>,
    std::enable_if_t<D<T>::value, E<T>>,
    F<T>
>;
```

**建议**：在头文件中保持别名模板简单，复杂的类型计算放在源文件中。

------

## 小结

别名模板是现代C++类型系统的重要组成部分：

| 特性 | typedef | using |
|------|---------|-------|
| 基本别名 | 支持 | 支持（更清晰） |
| 函数指针 | 支持 | 支持（更清晰） |
| 模板别名 | **不支持** | **支持** |
| 模板模板参数 | 困难 | 直观 |
| 可读性 | 较差 | 更好 |

**核心要点**：

1. **优先使用 `using`**：语法更清晰，功能更强大
2. **理解 `_t` 后缀约定**：`enable_if_t`、`conditional_t` 等都是别名模板
3. **别名模板是语法糖**：不产生运行时开销，只在编译期展开
4. **结合类型特征使用**：与 `type_traits` 配合实现编译期类型选择
5. **简化复杂类型**：将复杂的模板类型封装成易读的别名

**实践建议**：

1. 项目中统一使用 `using` 而非 `typedef`
2. 为常用的模板类型定义别名模板
3. 使用 `_t` 后缀约定标记类型别名
4. 头文件中保持别名模板简单，避免过度嵌套
5. 利用别名模板实现类型安全的接口

**下一章**，我们将探讨 **可变参数模板**，学习如何处理任意数量的模板参数，实现类型安全的格式化函数和灵活的委托系统。
