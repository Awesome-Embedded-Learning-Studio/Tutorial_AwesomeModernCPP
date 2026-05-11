---
title: "何时用C++、用哪些C++特性"
description: "探讨何时选择C++而非C，以及如何在嵌入式环境中明智地使用C++特性，包括推荐使用、折中和禁用的特性"
chapter: 0
order: 4
tags:
  - host
  - cpp-modern
  - beginner
  - 入门
  - 基础
difficulty: beginner
reading_time_minutes: 18
prerequisites: []
related: []
cpp_standard: [11, 14, 17, 20]
platform: host
---

# 何时用 C++、用哪些 C++ 特性

说实话，每次看到嵌入式圈子里爆发"C vs C++"的圣战，笔者都觉得挺无奈的。争论往往很快就滑向信仰层面——用 C 的觉得 C++ 是邪教，用 C++ 的觉得 C 是原始社会。但真正的问题是：我们手头的这个项目，在这块硬件上，用这门语言划不划算？这个问题没有任何人能替你回答，但我可以分享一下在实际项目中踩出来的经验，帮大家少走点弯路。

本章我们要搞清楚两件事：第一，什么样的项目值得上 C++；第二，上了 C++ 之后，哪些特性我们应该大胆用，哪些要谨慎，哪些能不用就不用。

## 什么时候 C++ 值得上场

先说大前提：如果你的项目代码规模超过了几万行，而且包含多个需要清晰接口边界的子系统，那 C++ 的优势就会开始显现。在 C 语言里，维护这种规模的项目当然也能做，但你需要团队投入大量精力来保持代码的组织结构——手动管理模块划分、手写接口抽象、手工保证类型安全。C++ 的类、命名空间和模板这些东西，恰恰是在语言层面帮你做了这些事。尤其是当多个子系统需要严格的接口定义时，C++ 的类型系统能在编译期就帮你拦住一大批接口误用的错误，这在 C 语言里往往要到运行时才会暴露。

类型安全在安全关键型系统里更是直接关乎人命。汽车电子、医疗设备、航空航天——在这些领域，C 语言里到处可见的 `void*` 和隐式类型转换简直就是定时炸弹。C++ 的强类型系统、枚举类、引用语义和 `const` 正确性，能够从编译器层面阻止大量低级错误的发生。这不是什么花哨的理论优势，而是实打实地减少了 bug 进入产品的概率。

代码复用需求也是一个重要的考量因素。如果你的项目需要跨多个产品线复用组件，或者存在大量相似但不完全相同的功能模块，C++ 的模板机制就能发挥威力了——它可以在编译期生成类型安全的代码，零运行时开销。相比 C 语言里靠宏和 `void*` 拼凑出来的"泛型"，C++ 的方案既安全又优雅。

但有一说一，上 C++ 的前提是团队得有相应的技术储备。如果团队全员都只写过 C，连 RAII 都没听说过，也没有培训和代码审查的机制，那贸然上 C++ 十有八九会翻车。反之，如果团队里有熟悉现代 C++ 实践的成员，能制定并执行合理的编码规范，那 C++ 的优势才能真正落地。

### 什么时候 C 仍然是更好的选择

反过来讲，在一些场景下坚持用 C 反而是更务实的选择。当目标平台极端资源受限——比如 Flash 小于 32KB、RAM 小于 4KB 的低成本 MCU——这时候 C 语言的简洁性和可预测性就是最大的优势。代码规模很小（比如不到五千行）的简单应用，引入 C++ 反而会增加不必要的复杂度。另外，如果项目需要深度集成大量遗留 C 代码，或者目标平台的工具链对 C++ 支持不够完善（这种情况在一些小众芯片上并不少见），继续用 C 往往是最省心的决定。

## 我们的好朋友：推荐使用的核心特性

好了，假设你已经决定上 C++ 了。接下来我们要搞清楚哪些特性应该成为日常开发的基础工具箱。这些特性都符合零开销抽象原则——享受了更好的代码组织，但不需要为此付出运行时代价。

### 类和封装

类和封装是 C++ 最基本也最有价值的特性之一。我们直接看一个实际的例子——传感器驱动。在 C 里，你可能习惯这样写：

```c
// C 风格：全局变量 + 裸函数
volatile uint32_t* sensor_reg = (volatile uint32_t*)0x40010000;

void sensor_enable(void) {
    *sensor_reg |= 0x01;
}

uint16_t sensor_read(void) {
    return (uint16_t)(*sensor_reg >> 16);
}
```

这样做的问题很明显：`sensor_reg` 是个全局变量，任何地方都能直接操作它，没人拦得住。而 C++ 的做法是把寄存器地址和访问逻辑封装到类的内部，对外只暴露 `enable()` 和 `read()` 接口：

```cpp
class SensorDriver {
private:
    uint32_t base_address_;
    volatile uint32_t* const reg_;

public:
    explicit SensorDriver(uint32_t addr)
        : base_address_(addr),
          reg_(reinterpret_cast<volatile uint32_t*>(addr)) {}

    void enable() {
        *reg_ |= 0x01;
    }

    uint16_t read() const {
        return static_cast<uint16_t>(*reg_ >> 16);
    }
};
```

关键在于，编译器生成的代码和上面 C 版本的机器码几乎没有区别——成员函数默认就是内联的，性能上毫不逊色。但外部代码再也没法直接碰寄存器了，出错的可能性被大幅压缩。

### 命名空间

大型项目里的命名冲突是个让人头疼的问题，特别是当你集成了好几个第三方库之后。C 语言的传统做法是给函数名加前缀，比如 `gpio_init()`、`uart_init()`、`spi_init()`，能用但不够优雅。C++ 的命名空间提供了更系统化的解决方案——把相关的函数和类组织在逻辑分组里，名字冲突的问题从根本上消失了：

```cpp
namespace drivers {
namespace gpio {
    void init();
    void set_pin_mode(uint8_t pin, PinMode mode);
    bool read_pin(uint8_t pin);
}

namespace uart {
    void init(uint32_t baud_rate);
    void send(const uint8_t* data, size_t len);
}
}

// 调用时一目了然
drivers::gpio::init();
drivers::uart::init(115200);
```

最棒的是，命名空间是纯粹的编译期特性，不会产生任何运行时开销。

### 引用语义

相比指针，引用有两个关键优势：第一，引用不能为空，所以不需要做空指针检查；第二，引用的语法更清晰地表达了函数的意图。当我们需要传递一个较大的结构体但不想拷贝时，`const` 引用既高效又安全；当函数需要修改传入的参数时，非 `const` 引用能清楚地表明这个意图：

```cpp
// 用 const 引用传递大型结构体——避免拷贝，且不能被修改
void process_data(const SensorData& data) {
    uint16_t value = data.temperature;
    // ...
}

// 非 const 引用表明函数会修改参数
bool try_read(SensorData& output) {
    if (data_available()) {
        output.temperature = read_temperature();
        output.humidity = read_humidity();
        return true;
    }
    return false;
}
```

对比 C 语言的指针方式，我们省去了 `NULL` 检查，代码也更简洁。底层实现上引用通常就是指针，所以不会有额外的性能开销。

### 编译期计算（constexpr）

`constexpr` 是现代 C++ 在嵌入式开发中的杀手级特性。它让编译器在编译阶段就完成计算，生成的代码里直接就是结果值，运行时零开销。举个例子，计算串口波特率的分频系数：

```cpp
constexpr uint32_t calculate_baud_rate_divisor(uint32_t sysclk, uint32_t baud) {
    return sysclk / (16 * baud);
}

// 编译期就算好了，生成的代码里直接是结果值 39
constexpr uint32_t divisor = calculate_baud_rate_divisor(72000000, 115200);
```

传统做法是在运行时做除法，而用 `constexpr` 函数，这个除法在编译阶段就完成了。程序跑起来的时候，`divisor` 的值已经就是 `39` 了，不需要任何计算。这不仅提升了性能，也让代码的意图更加明确。它甚至可以直接用作数组的大小：

```cpp
constexpr size_t kBufferSize = calculate_baud_rate_divisor(1000, 10);
uint8_t buffer[kBufferSize];
```

### 强类型枚举（enum class）

C 语言的传统枚举有几个让人头疼的问题：它们会隐式转换为整数、不同枚举之间的值可以混用、枚举名会污染外层作用域。C++11 引入的 `enum class` 一次性解决了所有这些问题：

```cpp
enum class PinMode : uint8_t {
    kInput = 0,
    kOutput = 1,
    kAlternate = 2,
    kAnalog = 3
};

enum class PullMode : uint8_t {
    kNoPull = 0,
    kPullUp = 1,
    kPullDown = 2
};

void set_mode(uint8_t pin, PinMode mode) {
    switch (mode) {
        case PinMode::kInput:  /* ... */ break;
        case PinMode::kOutput: /* ... */ break;
        default: break;
    }
}
```

现在如果你试图传入错误的类型，编译器会直接报错，不会像 C 枚举那样默默接受然后给你一个运行时的惊喜：

```cpp
set_mode(5, PinMode::kOutput);        // 正确
// set_mode(5, PullMode::kPullUp);    // 编译错误：类型不匹配
// set_mode(5, 1);                    // 编译错误：不能隐式转换
```

而且编译器通常会把 `enum class` 优化成普通整数，性能完全没有损失。

## 模板：好刀别乱挥

模板是 C++ 最强大但也最容易被滥用的特性。在嵌入式环境里，我们需要在代码复用和代码膨胀之间找到平衡点。

### 简单模板：放心用

简单的函数模板通常是安全的，因为它们往往会被编译器内联，最终生成的代码和手写的特定类型版本完全一样：

```cpp
template<typename T>
inline void swap(T& a, T& b) {
    T temp = a;
    a = b;
    b = temp;
}

uint32_t x = 10, y = 20;
swap(x, y);  // 编译器生成 swap<uint32_t>
```

### 类模板：看场景

类模板在合适的场景下也很好用，典型例子是固定大小的环形缓冲区。通过把元素类型和大小作为模板参数，我们实现了通用但零开销的缓冲区：

```cpp
template<typename T, size_t N>
class CircularBuffer {
private:
    T buffer_[N];
    size_t head_ = 0;
    size_t tail_ = 0;
    size_t count_ = 0;

public:
    bool push(const T& item) {
        if (count_ >= N) return false;
        buffer_[tail_] = item;
        tail_ = (tail_ + 1) % N;
        ++count_;
        return true;
    }

    bool pop(T& item) {
        if (count_ == 0) return false;
        item = buffer_[head_];
        head_ = (head_ + 1) % N;
        --count_;
        return true;
    }

    size_t size() const { return count_; }
    bool empty() const { return count_ == 0; }
    bool full() const { return count_ >= N; }
};
```

由于大小在编译期确定，编译器可以做充分优化。

⚠️ 但有一个坑需要注意：每一个模板参数的不同组合都会生成一份独立的代码。如果你实例化了 `CircularBuffer<uint8_t, 64>` 和 `CircularBuffer<uint8_t, 128>`，Flash 里就会有两份几乎一样的代码。所以模板要用，但别滥用。

### SFINAE 和 if constexpr：能用但别搞复杂

更高级的模板技术，比如 SFINAE 和类型特征，在嵌入式环境里应该谨慎使用。C++17 的 `if constexpr` 相比传统 SFINAE 要清晰得多，如果确实需要根据类型选择不同的实现，优先用它：

```cpp
template<typename T>
void serialize(const T& value, uint8_t* buffer) {
    if constexpr (std::is_integral<T>::value) {
        // 整数类型：直接写入
        *reinterpret_cast<T*>(buffer) = value;
    } else if constexpr (std::is_floating_point<T>::value) {
        // 浮点类型：同样直接写入
        *reinterpret_cast<T*>(buffer) = value;
    }
}
```

只有真正需要编译期类型约束时才考虑这些技术，而且尽量保持简单。模板元编程在嵌入式里搞复杂了，连自己过两周都看不懂。

## 需要签免责声明的特性

有些 C++ 特性不是不能用，但需要格外小心。下面这些特性在嵌入式项目里就像双刃剑——用好了是利器，用不好就是定时炸弹。

### 构造函数和析构函数

简单、快速的构造和析构是完全没问题的。RAII 风格的资源管理就是最好的例子——在构造时获取资源，在析构时自动释放，既安全又优雅：

```cpp
class ScopedLock {
private:
    Mutex& mutex_;

public:
    explicit ScopedLock(Mutex& m) : mutex_(m) {
        mutex_.lock();
    }

    ~ScopedLock() noexcept {
        mutex_.unlock();
    }

    // 禁止拷贝和赋值
    ScopedLock(const ScopedLock&) = delete;
    ScopedLock& operator=(const ScopedLock&) = delete;
};
```

使用的时候非常简单，离开作用域自动释放，即使提前 `return` 也不会忘记解锁：

```cpp
void critical_section() {
    ScopedLock lock(global_mutex);
    // 临界区代码...
}  // 自动释放锁
```

⚠️ 但如果你在构造函数里搞动态内存分配（`new`）、调用可能失败的硬件初始化、或者在中断上下文里创建需要析构的对象，那就等着踩坑吧。正确的做法是保持构造函数简单，用一个显式的 `init()` 函数来处理可能失败的初始化：

```cpp
class GoodDriver {
    static constexpr size_t kBufferSize = 1024;
    uint8_t buffer_[kBufferSize];  // 栈上分配，不用 new
    bool initialized_ = false;

public:
    GoodDriver() = default;  // 简单的默认构造

    bool init() {
        if (!init_hardware()) {
            return false;
        }
        initialized_ = true;
        return true;
    }

    ~GoodDriver() noexcept = default;
};
```

另外，析构函数一定要标 `noexcept`——如果析构过程中抛异常，程序会直接调用 `std::terminate()`，在嵌入式里这就是死机。

### 异常处理：默认禁用

在嵌入式项目里，笔者的建议是直接通过 `-fno-exceptions` 编译选项把异常关掉。这不是偏见——关掉异常可以减少 10% 到 30% 的代码体积，消除不可预测的执行时间，避免栈展开带来的额外 RAM 开销。而且很多嵌入式工具链对异常的支持本身就不完善，出了问题根本没法调试。

那错误处理怎么办？用错误码。这虽然不如异常优雅，但它是可预测的、高效的，而且容易做最坏情况分析：

```cpp
enum class ErrorCode : uint8_t {
    kOk = 0,
    kInvalidParameter,
    kTimeout,
    kHardwareError,
    kBufferFull,
    kNotInitialized
};

ErrorCode init_sensor(uint8_t address) {
    if (address == 0 || address > 127) {
        return ErrorCode::kInvalidParameter;
    }

    if (!check_hardware()) {
        return ErrorCode::kHardwareError;
    }

    return ErrorCode::kOk;
}
```

对于需要同时返回值和错误状态的场景，可以用一个简单的结构体把两者分开：

```cpp
struct Result {
    ErrorCode error;
    uint16_t value;

    bool is_ok() const { return error == ErrorCode::kOk; }
};

Result read_sensor() {
    Result res;
    if (!is_initialized()) {
        res.error = ErrorCode::kNotInitialized;
        res.value = 0;
        return res;
    }
    res.error = ErrorCode::kOk;
    res.value = read_hardware_register();
    return res;
}
```

⚠️ 除非你的系统有充足的 Flash 和 RAM、实时性要求不严格、工具链完全支持异常、而且团队有丰富的异常使用经验——否则别碰异常。

### RTTI：直接关掉

运行时类型信息（RTTI）也应该默认禁用，用 `-fno-rtti` 编译选项。RTTI 会增加代码体积、需要额外的元数据存储、还会带来性能开销。绝大多数嵌入式场景下，如果你需要判断类型，在基类里加一个类型标识枚举就够了，完全不需要 `dynamic_cast`。

### 虚函数：限制使用

虚函数提供了运行时多态性，在设计驱动抽象层时确实有用。但代价是实实在在的：每个包含虚函数的对象都需要一个虚表指针（4 到 8 字节），虚函数调用比直接调用慢 5% 到 10%，还可能阻碍编译器的内联优化。

如果只是需要编译期多态，用模板参数传递具体类型就够了，零运行时开销。虚函数应该只用在确实需要运行时多态的场景，而且要避免在性能关键路径上频繁调用。

## 能不用就不用的特性

下面这些特性，在嵌入式环境里建议能不用就不用。

### 动态内存分配

`new`、`delete`、`malloc`、`free`——这些在桌面开发里习以为常的操作，在嵌入式系统里就是风险源。堆碎片化会导致运行一段时间后内存分配失败，而且这种失败极难复现和调试。动态分配的不确定执行时间也让最坏情况分析变得不可能。

替代方案是使用固定大小的数据结构。标准库的 `std::array` 是安全的选择，它不涉及动态内存。如果需要动态大小的容器，可以实现静态容量的版本，或者用内存池——预先分配固定数量的等大内存块，分配和释放都是 O(1)，而且不会碎片化。

### 大部分 STL 容器

`vector`、`map`、`unordered_map`、`string`——这些容器都依赖动态内存分配，在嵌入式环境里不适用。`shared_ptr` 的引用计数涉及原子操作，在某些平台上开销也不小。`iostream` 更是应该完全避免，一个简单的 `cout` 可能引入 50KB 以上的代码。

但标准库不是全都不能用。`std::array`、`<algorithm>` 里的算法（注意有些会分配临时内存）、`<type_traits>` 这类编译期工具、`<utility>` 里的 `move` 和 `forward`——这些都是零开销或低开销的好东西。

如果确实需要容器，可以看看 Embedded Template Library（ETL），它提供了不使用动态内存的固定大小容器，接口和 STL 兼容。

### 标准多线程库

`std::thread`、`std::mutex` 这些组件代码体积大，而且依赖操作系统的特定支持。在嵌入式系统里，通常直接用 RTOS 提供的原语——FreeRTOS 的任务、信号量和队列，或者 CMSIS-RTOS 的标准接口——这些经过了针对嵌入式环境的优化，占用资源更少。

## 最后说两句

选对特性只是第一步。在项目里真正落地，还需要建立明确的编码规范，规定哪些能用、哪些禁用、哪些需要审查。代码审查要成为标配，特别关注有没有偷偷用了禁用特性、模板是不是搞得太复杂导致代码膨胀、虚函数是不是出现在了不该出现的地方。静态分析工具能帮我们自动检测很多这类问题，别省这个功夫。

性能方面，定期检查编译后的二进制大小，确保没有意外膨胀。对性能关键的代码路径做实际测量，别光凭感觉。编译器的优化选项很多，但效果需要实测来验证——不要直接在生产环境里试，先在开发板上跑通了再说。

语言选择不是信仰问题，而是工程问题。用数据说话，按模块分层选用工具，制定并用自动化手段执行约束。记住一句话就够了：用对的工具做对的事，别把工具当信念。
