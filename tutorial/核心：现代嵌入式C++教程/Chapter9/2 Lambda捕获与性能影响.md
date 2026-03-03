# 嵌入式C++教程——Lambda捕获与性能影响

## 引言

上一章我们学习了Lambda的基本语法，但你可能心中还有一个疑问：那个捕获列表`[...]`到底是怎么回事？值捕获和引用捕获有什么区别？会不会影响性能？

这些问题在嵌入式开发中尤为重要——我们既要代码优雅，又要零开销。

> 一句话总结：**捕获决定了Lambda如何访问外部变量，不同的捕获方式有不同的性能和安全性考虑。**

------

## 捕获方式全解析

Lambda的捕获列表可以按以下方式使用：

| 捕获方式 | 语法 | 说明 |
|---------|------|------|
| 空捕获 | `[]` | 不捕获任何外部变量 |
| 值捕获 | `[x]` | 复制变量x的值 |
| 引用捕获 | `[&x]` | 引用变量x |
| 全值捕获 | `[=]` | 值捕获所有外部变量 |
| 全引用捕获 | `[&]` | 引用捕获所有外部变量 |
| 混合捕获 | `[x, &y]` | x值捕获，y引用捕获 |
| 初始化捕获 | `[x = expr]` | C++14，用表达式初始化捕获变量 |

让我们逐个深入理解。

------

## 值捕获：复制一份副本

值捕获会在Lambda对象中存储被捕获变量的副本：

```cpp
void example_value_capture() {
    int threshold = 100;

    // threshold被复制到Lambda对象中
    auto is_high = [threshold](int value) {
        return value > threshold;  // 使用的是副本
    };

    threshold = 200;  // 修改外部变量，不影响Lambda
    bool result = is_high(150);  // false，因为Lambda里的threshold还是100
}
```

**关键点**：
- Lambda创建时复制，之后外部修改不影响
- Lambda内部修改也不会影响外部
- 默认是`const`的，如果要修改需要加`mutable`

### mutable关键字

值捕获的变量默认是`const`，如果要在Lambda内修改，需要`mutable`：

```cpp
int counter = 0;
// ❌ 编译错误：counter是const
auto lambda1 = [counter]() { counter++; };

// ✅ 加上mutable
auto lambda2 = [counter]() mutable {
    counter++;  // 修改的是Lambda内的副本
    return counter;
};
```

**嵌入式场景**：值捕获适合配置参数，确保线程安全：

```cpp
class UARTDriver {
public:
    void send_break(uint32_t duration_us) {
        // duration_us被捕获，确保发送过程中不会被修改
        auto send = [this, duration_us]() {
            *ctrl_reg |= BREAK_ENABLE;
            delay_microseconds(duration_us);  // 安全的duration值
            *ctrl_reg &= ~BREAK_ENABLE;
        };
        send();
    }
};
```

------

## 引用捕获：共享原始变量

引用捕获让Lambda直接访问外部变量，而不是复制：

```cpp
void example_ref_capture() {
    int sum = 0;

    // 引用捕获sum
    auto accumulator = [&sum](int value) {
        sum += value;  // 直接修改外部sum
    };

    accumulator(10);
    accumulator(20);
    // sum = 30
}
```

**关键点**：
- Lambda持有外部变量的引用，不是副本
- 外部修改会影响Lambda，反之亦然
- 需要确保外部变量生命周期比Lambda长

### 生命周期陷阱

引用捕获的最大风险是悬垂引用：

```cpp
// ❌ 危险：返回的Lambda引用了局部变量
auto make_counter() {
    int count = 0;
    return [&count]() { return ++count; };  // count已销毁！
}

// ❌ 危险：存储到函数对象后使用
std::function<void()> store_lambda() {
    int local = 42;
    return [&local]() { /* ... */ };  // local即将销毁
}
```

**安全实践**：确保引用捕获的变量生命周期足够长：

```cpp
class Device {
public:
    void init() {
        // this的生命周期比Lambda长，安全
        auto handler = [this]() {
            this->status = READY;
        };
        register_callback(handler);
    }
private:
    Status status;
};
```

------

## 全捕获：一网打尽

当需要捕获的变量很多时，可以一次性捕获所有：

```cpp
void example_full_capture() {
    int a = 1, b = 2, c = 3, d = 4;

    // [=] 全部值捕获
    auto lambda1 = [=]() {
        return a + b + c + d;  // a,b,c,d都是副本
    };

    // [&] 全部引用捕获
    auto lambda2 = [&]() {
        a++; b++;  // 直接修改外部变量
    };
}
```

**混合捕获**：可以指定某些变量用特殊方式：

```cpp
void example_mixed_capture() {
    int threshold = 100;
    int count = 0;
    double factor = 1.5;

    // threshold和factor值捕获，count引用捕获
    auto process = [threshold, factor, &count](int value) {
        if (value > threshold) {
            count++;
            return static_cast<int>(value * factor);
        }
        return value;
    };
}
```

**嵌入式建议**：避免使用`[&]`全引用捕获，容易无意捕获不该捕获的变量。

------

## 初始化捕获（C++14）：更灵活的捕获方式

C++14引入了初始化捕获，允许在捕获时进行任意表达式计算：

```cpp
void example_init_capture() {
    int base = 10;

    // 捕获base+5的结果，而不是base本身
    auto lambda = [value = base + 5]() {
        return value * 2;  // value是15
    };

    // 捕获移动的类型
    std::unique_ptr<int> ptr = std::make_unique<int>(42);
    auto lambda2 = [p = std::move(ptr)]() {
        return *p;  // p是移动进来的
    };
}
```

**嵌入式场景**：捕获计算后的配置：

```cpp
void configure_timer(int frequency_hz) {
    // 捕获计算好的寄存器值，而不是频率
    auto setup = [prescaler = SystemClock / frequency_hz - 1]() {
        *TIM_PSC = prescaler;
        *TIM_ARR = 999;
    };
    setup();
}
```

**比mutable更清晰**：

```cpp
// C++11写法：需要mutable
int x = 0;
auto lambda1 = [x]() mutable {
    x += 1;
    return x;
};

// C++14写法：初始化捕获，语义更清晰
auto lambda2 = [counter = 0]() {
    counter += 1;
    return counter;
};
```

------

## 性能影响：到底有没有开销？

这是嵌入式开发者最关心的问题。让我们从汇编层面分析。

### 值捕获的开销

值捕获本质上是把变量存为Lambda对象的成员变量：

```cpp
int threshold = 100;
auto lambda = [threshold](int x) { return x > threshold; };
```

编译器大致生成类似这样的代码：

```cpp
struct LambdaType {
    int threshold;  // 成员变量存储捕获的值

    bool operator()(int x) const { return x > threshold; }
};

LambdaType lambda{threshold};  // 构造时复制
```

**性能分析**：
- Lambda对象大小 = 所有值捕获变量的大小之和
- 构造时有复制开销
- 调用时有额外参数（捕获的成员），但内联后无开销

### 验证：汇编层面的零开销

```cpp
// 示例代码
int threshold = 100;
auto is_high = [threshold](int x) { return x > threshold; };
int result = is_high(150);
```

在`-O2`优化下，这会被完全内联为：

```asm
; 伪汇编
mov eax, 150
cmp eax, 100
setg al
```

**结论**：只要Lambda可内联，值捕获在调用时**零开销**。

### 引用捕获的开销

引用捕获存储的是指针：

```cpp
struct LambdaType {
    int* threshold_ptr;  // 指针成员

    bool operator()(int x) const { return x > *threshold_ptr; }
};
```

**性能分析**：
- Lambda对象大小 = 所有引用捕获变量的指针大小之和
- 调用时需要解引用，可能影响优化
- 但内联后通常也能优化掉

**结论**：引用捕获在调用时也有**接近零开销**（多一层间接访问）。

------

## 何时选择哪种捕获方式

### 选择值捕获的情况

1. **配置参数**：Lambda执行期间参数不应改变
   ```cpp
   auto send_bytes = [timeout = 1000](const uint8_t* data, size_t len) {
       // timeout在发送过程中不变
   };
   ```

2. **小型可复制对象**：`int`、`float`、简单结构体
   ```cpp
   auto scale = [factor = 1.5f](int x) { return x * factor; };
   ```

3. **线程安全需求**：多线程环境下确保数据不被修改
   ```cpp
   std::vector<std::thread> threads;
   for (int i = 0; i < 4; ++i) {
       threads.emplace_back([i]() {  // 值捕获，每个线程有自己的i
           process(i);
       });
   }
   ```

### 选择引用捕获的情况

1. **大型对象**：避免复制开销
   ```cpp
   std::array<int, 1024> big_array;
   auto process = [&big_array](int index) {
       big_array[index] *= 2;
   };
   ```

2. **需要修改外部变量**：累加器、状态更新
   ```cpp
   int sum = 0;
   std::for_each(vec.begin(), vec.end(), [&sum](int x) {
       sum += x;
   });
   ```

3. **this指针**：成员函数Lambda
   ```cpp
   class MyClass {
       void method() {
           auto lambda = [this]() { this->member = 42; };
       }
   };
   ```

### 选择初始化捕获的情况（C++14）

1. **需要移动的类型**：`unique_ptr`、`string`
   ```cpp
   auto task = [buf = std::move(buffer)]() {
       process_buffer(buf);
   };
   ```

2. **计算后的值**：避免重复计算
   ```cpp
   auto calc = [prescale = calc_prescale(freq)]() {
       *REG = prescale;
   };
   ```

------

## 嵌入式场景实战

### 场景1：中断回调安全捕获

```cpp
class ButtonDriver {
public:
    void init() {
        // 注册中断处理，值捕获确保安全
        register_irq(IRQ_GPIO, [this]() {
            // 需要volatile访问硬件
            if (*GPIO_STATUS & (1 << pin)) {
                *GPIO_STATUS = (1 << pin);  // 清除中断标志
                debounce_count++;
                pending_press = true;
            }
        });
    }

private:
    int pin = 5;
    volatile int debounce_count = 0;
    volatile bool pending_press = false;
};
```

### 场景2：DMA传输配置

```cpp
void start_dma_transfer(const uint8_t* src, uint8_t* dst, size_t size) {
    // 捕获所有参数，确保传输过程中参数稳定
    auto config = [src, dst, size]() {
        *DMA_SRC = reinterpret_cast<uint32_t>(src);
        *DMA_DST = reinterpret_cast<uint32_t>(dst);
        *DMA_CNT = size;
        *DMA_CTRL = DMA_EN | DMA_INT_EN;
    };

    config();  // 应用配置
}
```

### 场景3：状态机Lambda

```cpp
class StateMachine {
public:
    void update() {
        // 初始化捕获，Lambda有自己的状态变量
        auto handle_state = [state = current_state, count = 0]() mutable {
            switch (state) {
                case IDLE:
                    count = 0;
                    state = RUNNING;
                    break;
                case RUNNING:
                    count++;
                    if (count > 100) state = DONE;
                    break;
                case DONE:
                    // ...
                    break;
            }
            return state;
        };

        current_state = handle_state();
    }

private:
    enum State { IDLE, RUNNING, DONE };
    State current_state = IDLE;
};
```

------

## 避免的陷阱

### 陷阱1：捕获this的潜在问题

```cpp
class Device {
    std::string name = "sensor";

    // ❌ 如果this指向的对象被销毁，Lambda中的this悬垂
    auto get_name_lambda() {
        return [this]() { return name; };
    }
};

// 正确做法：捕获需要的成员，而不是整个this
auto get_name_lambda_safe() {
    return [name = this->name]() { return name; };
}
```

### 陷阱2：循环中的引用捕获

```cpp
std::vector<std::function<void()>> handlers;

// ❌ 所有Lambda引用同一个i，且循环结束后i失效
for (int i = 0; i < 5; ++i) {
    handlers.push_back([&i]() { use(i); });
}

// ✅ 值捕获，每个Lambda有自己的i
for (int i = 0; i < 5; ++i) {
    handlers.push_back([i]() { use(i); });
}
```

### 陷阱3：隐式捕获的隐患

```cpp
int config = 100;
int temp = 50;

// ❌ [&]捕获了所有变量，包括不需要的temp
auto lambda1 = [&]() { return config > 50; };

// ✅ 明确指定需要捕获的变量
auto lambda2 = [&config]() { return config > 50; };
```

------

## 小结

Lambda捕获机制的关键要点：

1. **值捕获**：安全但复制，适合小型不变数据
2. **引用捕获**：零拷贝但有生命周期要求，适合大型对象
3. **初始化捕获**：C++14最灵活，支持移动和表达式计算
4. **性能影响**：内联后接近零开销，对象大小取决于捕获内容

在嵌入式开发中：
- 优先使用值捕获或初始化捕获
- 避免全引用捕获`[&]`
- 注意捕获变量的生命周期
- 善用C++14初始化捕获处理移动语义

Lambda与捕获机制的合理使用，能让你的代码既优雅又高效。

---

## 导航

[← 上一篇 | Lambda表达式基础](<1 Lambda表达式基础.md>) | [下一篇 | std::function vs 函数指针 →](<3 std function vs 函数指针.md>)
