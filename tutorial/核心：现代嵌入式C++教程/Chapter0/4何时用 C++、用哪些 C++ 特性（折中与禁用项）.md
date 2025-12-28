# 何时用 C++、用哪些 C++ 特性（折中与禁用项）

所以，我们打算在什么地方上C++这是一个比较严重的问题。搞清楚这个事情对我们后续的开发非常重要。毕竟我们必须确定在这个地方上使用C++是合适的，我们才能继续我们的话题。





### 引言

在嵌入式系统开发领域，编程语言的选择往往是一个充满争议却又至关重要的架构决策。传统观念认为C语言是嵌入式开发的不二之选，但随着嵌入式系统复杂度的不断提升，C++凭借其强大的抽象能力和零开销抽象原则，正在越来越多的嵌入式项目中发挥重要作用。然而，C++并非在所有场景下都是最优选择，即便选择了C++，也需要对其特性进行精心筛选。本章将系统性地探讨何时应该选择C++，以及在资源受限的嵌入式环境中应该如何明智地使用C++特性。

### 何时选择C++而非C

当项目的系统复杂度达到一定程度时，C++的优势就会开始显现。通常来说，当代码规模超过若干万行时，项目往往需要更清晰的模块划分和抽象层次。此时，C语言虽然依然可以胜任，但需要开发团队投入大量精力来维护代码的组织结构，而C++的类、命名空间和模板等特性能够在语言层面提供更好的支持。特别是当系统包含多个子系统，且这些子系统需要明确的接口定义时，C++的类型系统能够在编译期就捕获许多潜在的接口误用问题。

对于安全关键型系统，比如汽车电子、医疗设备或航空航天领域的应用，类型安全的重要性怎么强调都不为过。C++的强类型系统能够在编译期进行更严格的检查，从而减少运行时错误的可能性。相比C语言中大量使用的void指针和隐式类型转换，C++的类型系统能够防止许多低级错误，这在安全关键应用中具有不可估量的价值。此外，C++的枚举类、引用语义和const正确性等特性，都能够帮助开发者构建更加健壮的系统。

当项目面临大量代码复用需求时，C++同样展现出其独特价值。在需要跨多个项目复用组件，或者存在大量相似但不完全相同的功能模块时，C++的模板机制能够提供参数化的数据结构和算法。这种编译期的代码生成能力，使得开发者可以编写一次代码而在多个场景下复用，同时保持类型安全和零运行时开销。相比C语言中通过宏和void指针实现的"泛型"，C++的解决方案更加优雅和安全。

然而，选择C++的前提条件之一是团队必须具备相应的技术能力。如果团队成员熟悉现代C++实践，能够建立并遵守合理的编码规范，并且可以进行有效的代码审查，那么C++的诸多优势才能真正发挥出来。反之，如果团队完全缺乏C++经验且没有培训资源，贸然引入C++可能会适得其反。

相对应地，在某些场景下坚持使用C语言可能是更明智的选择。当系统处于极端资源受限的环境，比如Flash容量小于32KB、RAM小于4KB时，C语言的简洁性和可预测性就显得尤为重要。对于代码规模较小的项目，比如少于五千行代码的简单应用，引入C++可能会带来不必要的复杂度。此外，如果项目需要与大量遗留的C代码集成，或者目标平台的工具链对C++支持不够完善，继续使用C语言往往是更务实的决策。

### 推荐使用的核心特性

在选择使用C++之后，首要任务是确定哪些特性应该成为日常开发的基础。类和封装是C++最基本也是最有价值的特性之一。通过将数据和操作数据的函数封装在类中，可以有效减少全局变量的使用，建立明确的接口边界。更重要的是，这种封装完全符合零开销抽象原则，编译器生成的代码与手工编写的C代码在性能上没有任何差异。

以传感器驱动为例，我们可以创建一个封装了硬件寄存器访问的类。这个类将寄存器的基地址作为私有成员，通过构造函数初始化，并提供enable和read等公共接口：

```cpp
class SensorDriver {
private:
    uint32_t baseAddress;
    volatile uint32_t* const reg;
    
public:
    explicit SensorDriver(uint32_t addr) 
        : baseAddress(addr), 
          reg(reinterpret_cast<volatile uint32_t*>(addr)) {}
    
    void enable() { 
        *reg |= 0x01; 
    }
    
    uint16_t read() const { 
        return static_cast<uint16_t>(*reg >> 16); 
    }
};
```

相比C语言中通过全局变量和函数指针实现的方案，这种方式不仅代码更加清晰，而且由于成员函数默认内联，性能上也毫不逊色。关键是，这种封装使得硬件细节完全隐藏在类的内部，外部代码无法直接访问寄存器，从而大大降低了出错的可能性。

命名空间是另一个应该在嵌入式C++项目中广泛使用的特性。在大型项目中，命名冲突是一个常见问题，特别是当集成多个第三方库时。C语言通常通过函数名前缀来解决这个问题，但这种方式既不优雅也容易出错。C++的命名空间提供了更加系统化的解决方案，可以将相关的函数、类和变量组织在逻辑分组中：

```cpp
namespace drivers {
    namespace gpio {
        void init();
        void setPinMode(uint8_t pin, PinMode mode);
        bool readPin(uint8_t pin);
    }
    
    namespace uart {
        void init(uint32_t baudRate);
        void send(const uint8_t* data, size_t len);
    }
}

// 使用时
drivers::gpio::init();
drivers::uart::init(115200);
```

将GPIO相关的所有接口放在drivers::gpio命名空间下，既能避免命名冲突，又使代码的组织结构一目了然。最重要的是，命名空间是一个纯粹的编译期特性，不会产生任何运行时开销。

引用语义是C++相对于C的一个重要改进。相比指针，引用有两个关键优势。首先，引用不能为空，这意味着在使用引用时不需要进行空指针检查，使得代码更加简洁。其次，引用的语法更加清晰，能够更明确地表达函数的意图：

```cpp
// 使用const引用传递大型结构体，避免拷贝
void processData(const SensorData& data) {
    // data不会被拷贝，也不能被修改
    uint16_t value = data.temperature;
}

// 使用非const引用表明函数会修改参数
bool tryRead(SensorData& output) {
    if (dataAvailable()) {
        output.temperature = readTemperature();
        output.humidity = readHumidity();
        return true;
    }
    return false;
}

// 对比C语言的指针方式
void processData_c(const SensorData* data) {
    if (data == NULL) return;  // 需要空指针检查
    uint16_t value = data->temperature;
}
```

当我们想要传递一个大型结构体但又不希望拷贝时，使用const引用既高效又安全。当函数需要修改传入的参数时，使用非const引用能够清楚地表明这一意图。在底层实现上，引用通常就是指针，因此不会带来额外的性能开销。

编译期计算是现代C++的一个强大特性，在嵌入式开发中尤其有用。通过constexpr关键字，我们可以让编译器在编译时完成复杂的计算，从而实现真正的零运行时开销：

```cpp
constexpr uint32_t calculateBaudRateDivisor(uint32_t sysclk, uint32_t baud) {
    return sysclk / (16 * baud);
}

// 编译期计算，生成的代码中直接是结果值39
constexpr uint32_t divisor = calculateBaudRateDivisor(72000000, 115200);

// 也可以用于数组大小
constexpr size_t bufferSize = calculateBaudRateDivisor(1000, 10);
uint8_t buffer[bufferSize];

// 对比传统运行时计算
uint32_t divisor_runtime = 72000000 / (16 * 115200);  // 运行时除法
```

比如计算串口波特率分频系数这样的场景，传统做法是在运行时进行除法运算，而使用constexpr函数可以让这个计算在编译期完成，生成的代码直接就是计算结果。这不仅提升了性能，也使得代码的意图更加明确。

强类型枚举是C++11引入的一个重要特性，它解决了传统C枚举的诸多问题：

```cpp
// 强类型枚举
enum class PinMode : uint8_t {
    Input = 0,
    Output = 1,
    Alternate = 2,
    Analog = 3
};

enum class PullMode : uint8_t {
    NoPull = 0,
    PullUp = 1,
    PullDown = 2
};

void setMode(uint8_t pin, PinMode mode) {
    // 类型安全，不会接受错误的枚举类型
    switch (mode) {
        case PinMode::Input: /* ... */ break;
        case PinMode::Output: /* ... */ break;
        default: break;
    }
}

// 传统C枚举的问题
enum OldPinMode { INPUT = 0, OUTPUT = 1 };
enum OldPullMode { NO_PULL = 0, PULL_UP = 1 };

void oldSetMode(uint8_t pin, OldPinMode mode) {
    // 危险：可以传入错误的枚举或整数
    oldSetMode(5, NO_PULL);  // 编译通过，但语义错误
    oldSetMode(5, 99);       // 编译通过，运行时错误
}

// 使用enum class
setMode(5, PinMode::Output);        // 正确
// setMode(5, PullMode::PullUp);    // 编译错误，类型不匹配
// setMode(5, 1);                   // 编译错误，不能隐式转换
```

使用enum class定义的枚举不会隐式转换为整数，这避免了许多潜在的类型错误。同时，强类型枚举的作用域独立，不会污染外层命名空间。在定义硬件引脚模式或者协议状态时，使用enum class可以获得类型安全的好处，而编译器通常会将其优化为普通整数，因此不会有性能损失。

### 模板特性的适度使用

模板是C++最强大但也最容易被滥用的特性。在嵌入式环境中，我们需要在代码复用和代码膨胀之间找到平衡点。简单的函数模板通常是安全的选择：

```cpp
template<typename T>
inline void swap(T& a, T& b) {
    T temp = a;
    a = b;
    b = temp;
}

// 使用时自动推导类型
uint32_t x = 10, y = 20;
swap(x, y);  // 编译器生成 swap<uint32_t>

float f1 = 1.5f, f2 = 2.5f;
swap(f1, f2);  // 编译器生成 swap<float>
```

比如实现一个类型安全的swap函数。这种函数模板通常会被编译器内联，最终生成的代码和手写的类型特定版本完全相同。关键是要保持模板的简单性，避免复杂的模板元编程技巧。

类模板在合适的场景下也能发挥重要作用，典型的例子是固定大小的环形缓冲区：

```cpp
template<typename T, size_t N>
class CircularBuffer {
private:
    T buffer[N];
    size_t head = 0;
    size_t tail = 0;
    size_t count = 0;
    
public:
    bool push(const T& item) {
        if (count >= N) return false;
        buffer[tail] = item;
        tail = (tail + 1) % N;
        count++;
        return true;
    }
    
    bool pop(T& item) {
        if (count == 0) return false;
        item = buffer[head];
        head = (head + 1) % N;
        count--;
        return true;
    }
    
    size_t size() const { return count; }
    bool empty() const { return count == 0; }
    bool full() const { return count >= N; }
};

// 使用示例
CircularBuffer<uint8_t, 64> rxBuffer;   // 64字节的接收缓冲区
CircularBuffer<Message, 16> msgQueue;   // 16个消息的队列

uint8_t byte;
if (rxBuffer.push(0x42)) {
    // 成功
}
if (rxBuffer.pop(byte)) {
    // 读取成功
}
```

通过将元素类型和大小作为模板参数，我们可以实现一个通用的环形缓冲区类，它可以用于不同的数据类型而不需要重复编写代码。由于缓冲区大小在编译期确定，编译器可以进行充分的优化，性能不会比手写的特定版本差。但需要注意的是，每一个模板参数的不同组合都会生成一份独立的代码，因此要避免过度实例化导致的代码膨胀。

SFINAE和类型特征属于更高级的模板技术，在嵌入式环境中应该谨慎使用：

```cpp
#include <type_traits>

// 使用SFINAE限制函数只接受整数类型
template<typename T>
typename std::enable_if<std::is_integral<T>::value, void>::type
processValue(T value) {
    // 只有整数类型会实例化这个函数
    uint32_t result = static_cast<uint32_t>(value) * 2;
}

// 或使用C++17的if constexpr（更清晰）
template<typename T>
void serialize(const T& value, uint8_t* buffer) {
    if constexpr (std::is_integral<T>::value) {
        // 整数类型的序列化
        *reinterpret_cast<T*>(buffer) = value;
    } else if constexpr (std::is_floating_point<T>::value) {
        // 浮点类型的序列化
        *reinterpret_cast<T*>(buffer) = value;
    }
}
```

虽然它们能够实现编译期的类型约束和函数重载，但也会增加代码的复杂度和编译时间。只有在确实需要根据类型特征选择不同实现时，才应该考虑使用这些技术。即便使用，也应该尽量保持简单，避免过度设计。

### 需要折中的特性

构造函数和析构函数是C++面向对象编程的基础，但在嵌入式环境中需要格外小心。简单、快速的构造和析构通常是可以接受的，比如实现一个RAII风格的互斥锁包装类：

```cpp
class Mutex {
    volatile uint32_t lockFlag;
public:
    void lock() {
        while (__sync_lock_test_and_set(&lockFlag, 1)) {
            // 自旋等待
        }
    }
    void unlock() {
        __sync_lock_release(&lockFlag);
    }
};

class ScopedLock {
private:
    Mutex& mutex;
    
public:
    explicit ScopedLock(Mutex& m) : mutex(m) { 
        mutex.lock(); 
    }
    
    ~ScopedLock() noexcept { 
        mutex.unlock(); 
    }
    
    // 禁止拷贝和赋值
    ScopedLock(const ScopedLock&) = delete;
    ScopedLock& operator=(const ScopedLock&) = delete;
};

// 使用示例
Mutex globalMutex;

void criticalSection() {
    ScopedLock lock(globalMutex);  // 自动获取锁
    // 执行临界区代码
    // ...
}  // 离开作用域时自动释放锁，即使发生错误返回
```

这个类在构造时获取锁，在析构时释放锁，既简洁又安全。然而，复杂的初始化链、在构造函数中分配动态内存、以及在中断上下文中创建需要析构的对象，这些都是应该避免的做法：

```cpp
// 禁止的做法
class BadDriver {
    uint8_t* buffer;
public:
    BadDriver() {
        buffer = new uint8_t[1024];  // 禁止：动态内存分配
        initHardware();               // 危险：如果失败怎么办？
    }
    ~BadDriver() {
        delete[] buffer;
    }
};

// 推荐的做法
class GoodDriver {
    static constexpr size_t BUFFER_SIZE = 1024;
    uint8_t buffer[BUFFER_SIZE];  // 静态分配
    bool initialized = false;
    
public:
    GoodDriver() = default;  // 简单的默认构造
    
    bool init() {  // 显式初始化函数
        if (!initHardware()) {
            return false;
        }
        initialized = true;
        return true;
    }
    
    ~GoodDriver() noexcept = default;
};
```

特别需要注意的是，析构函数应该始终是noexcept的，因为析构过程中抛出异常会导致程序终止。

异常处理是C++中争议最大的特性之一。在大多数嵌入式项目中，默认立场应该是禁用异常。通过编译选项-fno-exceptions可以完全关闭异常支持，这不仅能够减少10%到30%的代码体积，还能避免不可预测的执行时间和栈展开带来的额外开销。更重要的是，许多嵌入式工具链对异常的支持并不完善，可能会导致难以调试的问题。

作为替代方案，我们应该使用错误码返回值的方式处理错误：

```cpp
// 定义错误码枚举
enum class ErrorCode : uint8_t {
    Ok = 0,
    InvalidParameter,
    Timeout,
    HardwareError,
    BufferFull,
    NotInitialized
};

// 函数返回错误码
ErrorCode initSensor(uint8_t address) {
    if (address == 0 || address > 127) {
        return ErrorCode::InvalidParameter;
    }
    
    if (!checkHardware()) {
        return ErrorCode::HardwareError;
    }
    
    return ErrorCode::Ok;
}

// 使用示例
ErrorCode result = initSensor(0x42);
if (result != ErrorCode::Ok) {
    // 处理错误
    switch (result) {
        case ErrorCode::InvalidParameter:
            // 记录日志或报告错误
            break;
        case ErrorCode::HardwareError:
            // 尝试恢复或进入安全模式
            break;
        default:
            break;
    }
    return;
}

// 更复杂的情况：返回值和错误码分离
struct Result {
    ErrorCode error;
    uint16_t value;
    
    bool isOk() const { return error == ErrorCode::Ok; }
};

Result readSensor() {
    Result res;
    if (!isInitialized) {
        res.error = ErrorCode::NotInitialized;
        res.value = 0;
        return res;
    }
    
    // 读取硬件
    res.error = ErrorCode::Ok;
    res.value = readHardwareRegister();
    return res;
}

// 使用
Result res = readSensor();
if (res.isOk()) {
    processValue(res.value);
} else {
    handleError(res.error);
}
```

这种方式虽然不如异常优雅，但它是可预测的、高效的，而且容易进行最坏情况分析。只有在系统有充足的Flash和RAM资源，实时性要求不严格，工具链完全支持异常，且团队具备异常处理的最佳实践经验时，才应该考虑启用异常。

运行时类型信息RTTI也应该默认禁用。RTTI会增加代码体积，需要额外的元数据存储，还会带来性能开销。在绝大多数嵌入式场景下，我们可以通过在基类中添加类型标识字段来实现类似的功能。比如在消息类中添加一个类型枚举，通过检查这个枚举值就可以判断消息的具体类型，而不需要使用dynamic_cast。

虚函数和多态是一个需要权衡的特性。一方面，虚函数提供了运行时多态性，使得我们可以定义抽象接口，这在设计驱动层时特别有用。另一方面，虚函数带来的开销也是实实在在的。每个包含虚函数的对象都需要一个虚表指针，通常占用4到8个字节。虚函数调用比直接函数调用慢5%到10%，而且可能影响编译器的内联优化。因此，虚函数应该只在必须支持运行时多态时使用，并且要限制虚函数的数量，避免在性能关键路径上频繁调用虚函数。

作为替代方案，可以考虑使用编译期多态。通过模板参数传递具体的实现类型，可以实现类似多态的效果但没有任何运行时开销。这种技术在实现策略模式或者依赖注入时特别有用。虽然它会增加一些代码复杂度，但在性能敏感的场合往往是值得的。

### 明确禁用的特性

动态内存分配是嵌入式C++开发中最应该避免的特性。new、delete、malloc和free这些操作符在桌面应用中习以为常,但在嵌入式系统中却充满风险。堆碎片化会导致内存分配在运行一段时间后失败,而这种失败往往难以处理。动态分配的不确定执行时间使得系统难以进行最坏情况分析,这对实时系统来说是致命的。此外,当内存分配失败时,程序应该如何处理?在桌面系统中可以抛出异常,但我们已经禁用了异常。

替代方案是使用固定大小的数据结构。可以实现一个静态容量的vector类,它使用栈上的数组而不是堆内存。虽然这限制了容器的最大容量,但在嵌入式系统中,我们通常能够预估出所需的最大容量。另一个选择是实现内存池,预先分配固定数量的同样大小的内存块。这种方式虽然灵活性有限,但分配和释放都是O(1)操作,而且不会产生碎片。

标准模板库STL中的大部分容器都应该被禁用。vector、map、unordered_map和string这些容器都依赖动态内存分配,因此不适合在嵌入式环境中使用。shared_ptr虽然提供了智能的内存管理,但它的引用计数涉及原子操作,在某些平台上开销较大。iostream更是应该完全避免,因为它会导致极大的代码膨胀,一个简单的cout语句可能会引入五十KB以上的代码。

并非所有STL都不能用。标准库中的array是固定大小的数组包装,不涉及动态内存,可以安全使用。algorithm头文件中的算法通常也是可以的,但需要注意某些算法可能会分配临时内存。type_traits这样的编译期工具完全没有运行时开销,可以放心使用。utility中的move和forward等工具函数对于实现高效的值语义很有帮助。

对于需要容器的场景,可以考虑使用专门为嵌入式设计的库,比如Embedded Template Library(ETL)。这个库提供了固定大小的vector、queue、map等容器,它们的接口与STL兼容但不使用动态内存。或者,也可以根据项目需求自己实现轻量级的容器类。

标准的多线程库也应该被禁用。thread、mutex这些组件不仅代码体积大,而且依赖操作系统的特定支持。在嵌入式系统中,通常使用RTOS提供的原语,比如FreeRTOS的任务、信号量和队列,或者CMSIS-RTOS的标准接口。这些RTOS原语经过了针对嵌入式环境的优化,而且占用的资源更少。如果需要在任务间共享数据,应该使用RTOS提供的队列或邮箱机制,而不是标准库的shared_ptr或锁。

### 编码规范与最佳实践

选择了合适的特性只是第一步,如何在项目中有效地使用这些特性同样重要。首先应该建立明确的编码规范,规定哪些特性允许使用、哪些禁止使用、哪些需要经过审查才能使用。这个规范应该根据项目的具体情况制定,比如目标平台的资源约束、实时性要求、团队的技术水平等因素。

代码审查应该成为开发流程的必要环节。在审查时,要特别关注是否使用了被禁止的特性,模板是否被过度使用导致代码膨胀,以及虚函数调用是否出现在性能关键路径上。通过静态分析工具可以自动检测许多问题,比如是否使用了动态内存、是否启用了异常、以及代码体积是否超标。

性能测量和资源监控同样不可或缺。应该定期检查编译后的二进制文件大小,确保没有意外的膨胀。对于性能关键的代码路径,应该进行实际测量,而不是仅凭经验判断。现代编译器提供了许多优化选项,但这些优化的效果需要通过实际测试来验证。所以不要立马在生产环境中就进行尝试，先看看会不会出问题。
