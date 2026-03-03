# std::function vs 函数指针

## 引言

你在写一个事件处理系统，需要存储回调函数。传统C方式是用函数指针，但现代C++又给你一个`std::function`。哪个更合适？在资源受限的嵌入式环境下，这可不是"随便选一个"的问题。

函数指针是C时代的遗产，轻量、直接。`std::function`是C++11引入的通用函数包装器，能存储任何可调用对象——函数指针、Lambda、仿函数、`std::bind`的结果。

> 一句话总结：**函数指针零开销但功能受限，`std::function`功能强大但有运行时成本。嵌入式开发需要根据场景权衡。**

------

## 函数指针：轻量级的选择

函数指针是C语言传承下来的机制，直接指向代码地址，简单而高效。

### 基本用法

```cpp
// 普通函数
int add(int a, int b) {
    return a + b;
}

// 函数指针声明
int (*func_ptr)(int, int) = add;

// 使用
int result = func_ptr(3, 4);  // result = 7
```

用`typedef`或`using`简化：

```cpp
// C风格typedef
typedef int (*BinaryOp)(int, int);
BinaryOp op = add;

// C++11 using
using BinaryOp = int(*)(int, int);
BinaryOp op = add;
```

### 嵌入式场景示例

```cpp
// ADC转换完成回调
using ADCCallback = void(*)(int16_t value);

volatile uint32_t* const ADC_DR = reinterpret_cast<volatile uint32_t*>(0x4001204C);

void register_adc_callback(ADCCallback callback);

void my_adc_handler(int16_t value) {
    if (value > 4095) {
        // 处理溢出
    }
    // 处理正常值
}

void setup_adc() {
    register_adc_callback(my_adc_handler);
    *ADC_DR |= (1 << 0);  // 启动ADC
}
```

### 函数指针的优势

| 特性 | 说明 |
|------|------|
| **零开销** | 本质就是一个指针，`sizeof`通常等于指针大小 |
| **可预测** | 编译期确定，没有动态分配 |
| **直接调用** | 生成的汇编代码就是简单的`call`指令 |
| **ROM友好** | 可以放进ROM，适用于表驱动法 |

```cpp
// 表驱动法：函数指针数组非常适合嵌入式
struct StateHandler {
    int state;
    void (*handler)(void);
};

StateHandler state_table[] = {
    {0, handle_idle},
    {1, handle_running},
    {2, handle_error},
    {3, handle_shutdown}
};

void run_state_machine(int current_state) {
    for (const auto& entry : state_table) {
        if (entry.state == current_state) {
            entry.handler();  // 直接调用，零开销
            break;
        }
    }
}
```

### 函数指针的局限

```cpp
// ❌ 函数指针无法捕获上下文
class SensorManager {
    int sensor_id;

    // 函数指针不能存储this指针
    void setup() {
        // 错误：无法让成员函数指针携带this
        register_callback([](int v) {  // Lambda不是函数指针！
            this->process(v);
        });
    }
};
```

函数指针只能指向**静态函数**或**全局函数**，无法携带额外的上下文信息。这是它在面向对象设计中的致命缺陷。

------

## std::function：通用函数包装器

`std::function`是C++11引入的类型擦除容器，可以存储、复制和调用任何可调用对象。

### 基本用法

```cpp
#include <functional>

std::function<int(int, int)> func;

// 存储普通函数
func = add;
int r1 = func(3, 4);

// 存储Lambda
func = [](int a, int b) { return a * b; };
int r2 = func(3, 4);

// 存储仿函数
struct Multiplier {
    int factor;
    int operator()(int x) const { return x * factor; }
};
func = Multiplier{5};
int r3 = func(10);  // 返回50
```

### 存储捕获上下文的Lambda

这是`std::function`相对于函数指针的核心优势：

```cpp
class Button {
    int pin;
    int debounce_count = 0;

public:
    // 可以存储带捕获的Lambda
    std::function<void()> get_handler() {
        return [this]() {
            debounce_count++;
            // 访问成员变量
            if (*GPIO_IN & (1 << pin)) {
                // 处理按键
            }
        };
    }
};
```

### 嵌入式场景示例

```cpp
#include <functional>
#include <array>

class EventLoop {
public:
    using Callback = std::function<void()>;

    void register_timer(int id, uint32_t interval_ms, Callback cb) {
        timers[id] = {interval_ms, 0, std::move(cb)};
    }

    void tick(uint32_t delta_ms) {
        for (auto& timer : timers) {
            timer.elapsed += delta_ms;
            if (timer.elapsed >= timer.interval && timer.callback) {
                timer.callback();
                timer.elapsed = 0;
            }
        }
    }

private:
    struct Timer {
        uint32_t interval;
        uint32_t elapsed;
        Callback callback;
    };
    std::array<Timer, 8> timers;  // 最多8个定时器
};

// 使用：可以轻松捕获上下文
void setup_application() {
    EventLoop loop;
    int led_counter = 0;

    // 捕获led_counter
    loop.register_timer(0, 1000, [&led_counter]() {
        led_counter = (led_counter + 1) % 10;
        update_display(led_counter);
    });

    // 捕获this指针
    class Sensor {
    public:
        void start(EventLoop& loop) {
            loop.register_timer(1, 100, [this]() {
                this->read();
            });
        }
    };
}
```

### std::function的代价

| 特性 | 代价 |
|------|------|
| **内存占用** | 通常2-3个指针大小（存储函数指针+管理器+可能的分配器） |
| **动态分配** | 存储大型可调用对象时可能堆分配 |
| **间接调用** | 需要通过虚函数表式的机制调用 |
| **代码膨胀** | 每个不同的签名实例化模板，增加代码体积 |

```cpp
// 典型实现的大小（具体取决于编译器）
sizeof(std::function<void()>)      // 32位：16~32字节
                                   // 64位：32~64字节

sizeof(void(*)())                  // 32位：4字节
                                   // 64位：8字节
```

**小对象优化（SOO）**：大多数实现对小可调用对象（如捕获少量变量的Lambda）避免堆分配，但这仍然比函数指针大得多。

```cpp
// 小Lambda：可能不需要堆分配，但对象仍较大
auto small = [x = 42]() { return x * 2; };
std::function<int()> f = small;  // f的大小约32字节

// 大Lambda：几乎肯定需要堆分配
auto large = [big_array = std::array<int, 1000>()](){ /* ... */ };
std::function<void()> g = large;  // g+堆分配
```

------

## 性能对比：从汇编层面看

让我们从生成的代码角度对比两种机制。

### 函数指针的汇编输出

```cpp
int add(int a, int b) { return a + b; }

int (*func)(int, int) = add;
int result = func(3, 4);
```

在`-O2`优化下，这会生成类似以下的汇编：

```asm
; 伪汇编，ARM Cortex-M
ldr r0, =3         ; 第一个参数
ldr r1, =4         ; 第二个参数
ldr r2, =func      ; 加载函数指针
blx r2             ; 间接调用
; 结果在 r0 中
```

如果编译器能确定`func`指向`add`，甚至可能直接内联：

```asm
ldr r0, =3
ldr r1, =4
add r0, r0, r1     ; 直接内联 add
```

### std::function的汇编输出

```cpp
std::function<int(int, int)> func = add;
int result = func(3, 4);
```

生成的汇编更复杂：

```asm
; 伪汇编
ldr r0, =3         ; 参数
ldr r1, =4
ldr r2, =func      ; 加载std::function对象地址
ldr r3, [r2, #0]   ; 加载调用器指针（invoke函数）
blx r3             ; 间接调用invoke
; invoke内部再调用实际函数
```

`std::function`的调用链：
1. 调用`std::function`的内部调用器
2. 调用器调用存储的实际函数
3. 实际函数执行

**结论**：函数指针少一层间接调用，编译器更容易优化。

### 实测性能对比

```cpp
#include <benchmark>

void benchmark_function_pointer(benchmark::State& state) {
    int (*func)(int, int) = [](int a, int b) { return a + b; };
    int sum = 0;
    for (auto _ : state) {
        sum += func(1, 2);
    }
    benchmark::DoNotOptimize(sum);
}

void benchmark_std_function(benchmark::State& state) {
    std::function<int(int, int)> func = [](int a, int b) { return a + b; };
    int sum = 0;
    for (auto _ : state) {
        sum += func(1, 2);
    }
    benchmark::DoNotOptimize(sum);
}

// 典型结果（-O2优化）：
// function_pointer:  ~1 ns/call（内联后）
// std_function:     ~5-10 ns/call
```

内联后差异可能消失，但`std::function`的内联机会更少。

------

## 选择指南：什么时候用什么？

### 使用函数指针的场景

1. **表驱动法**：状态机、解析器、指令处理
   ```cpp
   struct OpcodeHandler {
       uint8_t opcode;
       void (*handler)(const uint8_t* args);
   };

   const OpcodeHandler opcode_table[] = {
       {0x01, handle_mov},
       {0x02, handle_add},
       {0x03, handle_sub},
   };
   ```

2. **中断处理表**：硬件向量表
   ```cpp
   using IRQHandler = void(*)();

   // 可以直接放进Flash
   const IRQHandler __attribute__((section(".isr_vector"))) irq_vector[48] = {
       reinterpret_cast<IRQHandler>(__stack_top),
       reset_handler,
       nmi_handler,
       // ...
   };
   ```

3. **简单的无状态回调**：不需要上下文的操作
   ```cpp
   void sort_array(int* arr, size_t len,
                   int (*compare)(int, int) = default_compare);
   ```

4. **ROM驻留数据**：需要放进Flash的表
   ```cpp
   const __attribute__((section(".rodata"))) DecoderEntry decoder[] = {
       {0x01, decode_instruction_a},
       {0x02, decode_instruction_b},
   };
   ```

### 使用std::function的场景

1. **需要捕获上下文的回调**：Lambda捕获
   ```cpp
   class Device {
       int id;
       void register_callbacks() {
           manager.on_data([this](const uint8_t* data, size_t len) {
               this->handle_data(data, len);
           });
       }
   };
   ```

2. **统一接口存储多种可调用对象**
   ```cpp
   class TaskScheduler {
       std::vector<std::function<void()>> tasks;
       void add_task(std::function<void()> task) {
           tasks.push_back(std::move(task));
       }
   };
   ```

3. **延迟调用/事件队列**：需要存储和传递
   ```cpp
   std::queue<std::function<void()>> event_queue;

   void post_event(std::function<void()> event) {
       event_queue.push(std::move(event));
   }
   ```

4. **C++标准库算法**：但优先用Lambda
   ```cpp
   // 可以用std::function，但Lambda更高效
   std::sort(v.begin(), v.end(),
             [](int a, int b) { return a > b; });
   ```

------

## 零开销替代方案：模板化设计

如果你既想要`std::function`的通用性，又想要函数指针的性能，模板化设计是答案：

### 模板参数

```cpp
// 不使用std::function，直接用模板
template<typename Callback>
void register_timer(int id, uint32_t interval_ms, Callback&& cb) {
    // 直接存储类型擦除前的原始类型
    timers[id] = Timer{interval_ms, 0, std::forward<Callback>(cb)};
}

// Timer定义为模板
template<typename Callback>
struct Timer {
    uint32_t interval;
    uint32_t elapsed;
    Callback callback;
};

// 问题：每个Callback类型实例化Timer，不能放在同一容器中
```

### 类型擦除的零开销实现

这是下一章的主题，简要说：手动实现小型类型擦除容器：

```cpp
template<typename Signature>
class Callback;

template<typename R, typename... Args>
class Callback<R(Args...)> {
    // 手写小型类型擦除，避免std::function的开销
    // 具体实现见下一章
};
```

------

## 嵌入式最佳实践

### 1. 热路径用函数指针

```cpp
// 高频调用：ADC采样中断
void __attribute__((interrupt)) ADC_IRQHandler() {
    static int (*handler)(int) = fast_adc_handler;  // 函数指针
    int value = *ADC_DR;
    handler(value);  // 零开销调用
}
```

### 2. 冷路径用std::function

```cpp
// 低频调用：配置更新
class ConfigManager {
    void on_update(std::function<void(const Config&)> cb) {
        update_callbacks.push_back(std::move(cb));
    }
};
```

### 3. 避免std::function的动态分配

```cpp
// ❌ 大Lambda捕获，会堆分配
std::function<void()> f = [big = std::array<int, 1000>()](){ /* ... */ };

// ✅ 小Lambda捕获，利用SOO
std::function<void()> f = [x = 42]() { return x * 2; };

// ✅ 或者直接用auto
auto f = [x = 42]() { return x * 2; };  // 完全零开销
```

### 4. 禁用堆时的选择

```cpp
// 如果完全禁用堆，std::function可能不可用
// 使用函数指针+用户数据指针的C风格模式

struct Callback {
    void (*func)(void* user_data);
    void* user_data;
};

void register_callback(Callback cb);

// 使用
void my_handler(void* data) {
    int* counter = static_cast<int*>(data);
    (*counter)++;
}

int counter = 0;
register_callback({my_handler, &counter});
```

------

## 小结

`std::function`和函数指针各有用武之地：

| 特性 | 函数指针 | std::function |
|------|----------|---------------|
| **开销** | 零（一个指针） | 小到中等（2-3个指针+可能的堆） |
| **通用性** | 只能指向静态/全局函数 | 任何可调用对象 |
| **上下文** | 无，需额外传递 | 可捕获任意上下文 |
| **类型安全** | 弱（容易不匹配） | 强（编译期检查） |
| **优化友好** | 高（易内联） | 中等（类型擦除阻碍优化） |
| **ROM友好** | 是 | 否 |

在嵌入式开发中的建议：

- **优先函数指针**：表驱动法、中断向量、热路径回调
- **谨慎std::function**：仅用于需要上下文捕获的冷路径
- **考虑模板**：如果能在编译期确定类型，用模板避免运行时成本
- **手动类型擦除**：对于需要两者优势的场景，考虑下一章的零开销实现

下一章我们将探讨如何实现零开销的回调机制，既保持`std::function`的通用性，又接近函数指针的性能。

---

## 导航

[← 上一篇 | 嵌入式C++教程——Lambda捕获与性能影响](<2 Lambda捕获与性能影响.md>) | [下一篇 | 回调机制的零开销实现 →](<4 回调机制的零开销实现.md>)
