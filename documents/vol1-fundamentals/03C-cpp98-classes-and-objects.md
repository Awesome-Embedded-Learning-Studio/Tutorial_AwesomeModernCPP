---
title: "C++98面向对象：类与对象深度剖析"
description: "从C结构体到C++类的跨越——访问控制、构造析构、初始化列表、this指针、静态成员、const成员函数、友元、explicit和mutable，讲清楚每一个细节"
chapter: 0
order: 3
tags:
  - cpp-modern
  - host
  - beginner
  - 入门
  - 基础
difficulty: beginner
reading_time_minutes: 30
prerequisites:
  - "C++98入门：命名空间、引用与作用域解析"
  - "C++98函数接口：重载与默认参数"
related:
  - "C++98面向对象：继承与多态"
  - "C++98运算符重载"
cpp_standard: [11, 14, 17, 20]
platform: host
---

# C++98面向对象：类与对象深度剖析

> 完整的仓库地址在 [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP) 中，您也可以光顾一下，喜欢的话给一个 Star 激励一下作者

类和对象是 C++ 面向对象编程的核心概念，但在嵌入式语境下，它们常常被误解成"重""慢""花里胡哨"。实际上，类并不等于复杂，OOP 也不等于必须上继承、多态那一套。**在资源紧张、业务逻辑清晰的嵌入式系统中，类最核心的价值只有一个：把"状态"和"操作状态的代码"绑在一起。**

换句话说，类的第一价值不是抽象，而是**约束**。

这一章我们会从 C 结构体出发，逐步过渡到 C++ 的类，把每一个关键概念都拆开来讲清楚——包括构造函数和析构函数、成员初始化列表、`this` 指针、静态成员、`const` 成员函数、友元，以及 `explicit` 和 `mutable` 这两个经常被忽略但非常有用的关键字。

## 1. 从 struct 到 class

### 1.1 C 结构体的局限

在 C 语言中，我们用结构体来组织数据，然后用独立的函数来操作这些数据。比如一个 LED 的控制代码，C 风格大概长这样：

```c
// C 风格：数据和操作分离
struct LED {
    int pin;
    bool state;
};

void led_init(struct LED* led, int pin) {
    led->pin = pin;
    led->state = false;
    gpio_init(pin, OUTPUT);
}

void led_on(struct LED* led) {
    led->state = true;
    gpio_write(led->pin, HIGH);
}

void led_off(struct LED* led) {
    led->state = false;
    gpio_write(led->pin, LOW);
}
```

这段代码能工作，但有一个结构性问题：`led_init`、`led_on`、`led_off` 这几个函数和 `struct LED` 之间的关联，**完全靠命名约定来维持**。没有任何语法层面的机制能阻止你写 `led_on(&uart_config)` 这种荒唐的调用——编译器不会报错，因为 `led_on` 接受的是 `struct LED*`，而你可能碰巧把一个错误的结构体指针传了进去。

### 1.2 C++ 的 class：把数据和操作绑在一起

C++ 的类解决了这个问题——它把数据（成员变量）和操作（成员函数）收拢到同一个语法单元里：

```cpp
class LED {
private:
    int pin;
    bool state;

public:
    LED(int pin_number) : pin(pin_number), state(false) {
        gpio_init(pin, OUTPUT);
    }

    void on() {
        state = true;
        gpio_write(pin, HIGH);
    }

    void off() {
        state = false;
        gpio_write(pin, LOW);
    }

    void toggle() {
        state = !state;
        gpio_write(pin, state ? HIGH : LOW);
    }

    bool is_on() const {
        return state;
    }
};
```

现在使用的时候，你只能通过 `LED` 类的公共接口来操作它：

```cpp
LED led(5);    // 构造时指定引脚号
led.on();      // 点亮
led.toggle();  // 切换状态
bool on = led.is_on();  // 查询状态
```

对比 C 版本，最明显的改善是：你不再需要手动传递结构体指针了。`led.on()` 这个调用天然就知道自己操作的是哪个 LED——因为 `on()` 是 `led` 对象的成员函数，编译器会自动把 `led` 的地址作为隐藏参数传递过去。这背后其实就是我们接下来要讲的 `this` 指针。

### 1.3 访问控制：public、private、protected

C++ 提供了三个访问控制关键字来管理类成员的可见性。

`private` 成员只有类自己的成员函数可以访问。在上面的 `LED` 类中，`pin` 和 `state` 是 `private` 的，这意味着你无法从类外部直接读写它们：

```cpp
LED led(5);
// led.pin = 10;   // 编译错误！pin 是 private 的
// led.state = true; // 编译错误！state 是 private 的
led.on();          // OK，on() 是 public 的
```

`private` 并不是为了"防黑客"，而是为了**在语法层面告诉使用者：哪些东西你不该碰**。你当然可以通过各种手段绕过它（比如指针强转、宏定义等），但那已经属于未定义行为的范畴了。对大多数工程代码来说，`private` 本身就是一种极强的自文档——它让阅读代码的人一眼就能分清"接口"和"实现细节"。

`public` 成员对所有代码都可见，构成类的外部接口。`protected` 成员对类自身和它的派生类可见——这个我们在讲继承的时候再详细讨论。

关于 `class` 和 `struct` 的区别，其实只有一个：`class` 的默认访问权限是 `private`，而 `struct` 的默认访问权限是 `public`。在语义上，`struct` 通常用来表达"一组数据的集合"（C 风格），而 `class` 用来表达"有行为的对象"。但编译器并不会强制你遵守这个惯例——你完全可以写一个所有成员都是 `public` 的 `class`，或者一个有成员函数的 `struct`。选择哪一个，更多是向读者传达你的设计意图。

## 2. 构造函数与析构函数

### 2.1 构造函数：把对象带入合法状态

构造函数是一种特殊的成员函数，它在对象创建时自动被调用，负责把对象带入一个**合法、可用的状态**。构造函数的名字和类名相同，没有返回类型（连 `void` 都没有），可以有参数，也支持重载。

我们看一个更完整的硬件资源管理的例子——一个 UART 端口封装类：

```cpp
class UARTPort {
private:
    int port_number;
    int baudrate;
    bool initialized;

public:
    // 构造函数：初始化 UART 硬件
    UARTPort(int port, int baud) : port_number(port), baudrate(baud), initialized(false) {
        // 配置硬件引脚复用
        configure_pins(port_number);
        // 设置波特率
        set_baudrate(baudrate);
        // 启用 UART 外设时钟
        enable_clock(port_number);

        initialized = true;
    }

    void send(const uint8_t* data, size_t length) {
        if (!initialized) return;
        // 发送数据
    }

    bool is_initialized() const {
        return initialized;
    }
};
```

使用的时候，对象一旦创建，就已经处于可用状态：

```cpp
UARTPort uart(1, 115200);  // 构造时完成全部硬件初始化
uart.send(data, sizeof(data));
// 离开作用域时...
```

构造函数的核心价值在于：**它消除了"忘记初始化"的可能性**。在 C 里，你可能会忘记调用 `uart_init()`，然后拿着一个未初始化的结构体去发送数据——后果不堪设想。而在 C++ 中，对象的创建和初始化是绑定在一起的，不可能出现"创建了但没初始化"的对象。

### 2.2 析构函数：在对象生命周期结束时做清理

析构函数是构造函数的"搭档"，它在对象销毁时自动被调用。析构函数的名字是 `~` 加类名，没有参数，也没有返回类型：

```cpp
class UARTPort {
private:
    int port_number;
    // ... 其他成员

public:
    UARTPort(int port, int baud) {
        // 初始化硬件
    }

    ~UARTPort() {
        // 关闭 UART
        disable_uart(port_number);
    }
};
```

在嵌入式系统中，析构函数特别适合用来释放硬件资源：关闭外设、释放 DMA 通道、恢复引脚为默认状态等。这种"构造时获取、析构时释放"的模式有一个著名的名字——**RAII (Resource Acquisition Is Initialization)**。RAII 是 C++ 资源管理的核心思想，我们会在后续的章节中专门深入讲解。现在只需要记住一点：**如果你在构造函数里获取了什么资源，就一定要在析构函数里释放它**。

对象的析构时机取决于它的存储方式。局部对象在离开作用域时析构，全局/静态对象在程序结束时析构，而通过 `new` 动态分配的对象只有在 `delete` 时才会析构。

### 2.3 默认构造函数

如果你没有为类定义任何构造函数，编译器会自动生成一个**默认构造函数**——一个无参的、什么都不做的构造函数。但只要你定义了任何一个构造函数（哪怕是有参数的），编译器就不再自动生成默认构造函数了。

```cpp
class Sensor {
private:
    int pin;

public:
    Sensor(int p) : pin(p) {}  // 定义了一个有参构造函数
    // 此时编译器不再生成默认构造函数
};

Sensor s1(5);   // OK
Sensor s2;      // 编译错误！没有默认构造函数可用
```

如果你既需要带参数的构造函数，又需要无参的默认构造，可以显式定义一个：

```cpp
class Sensor {
private:
    int pin;

public:
    Sensor() : pin(0) {}       // 默认构造函数
    Sensor(int p) : pin(p) {}  // 带参数的构造函数
};
```

## 3. 成员初始化列表

### 3.1 为什么要用初始化列表

在构造函数中，成员初始化列表是**初始化**类成员的首选方式。很多人习惯在构造函数体内用赋值语句来"初始化"成员变量，但这在 C++ 的语义下并不是真正的初始化——而是"先默认构造，再赋值"。对于某些类型的成员来说，这种"先构造再赋值"的方式甚至是不合法的。

来看两者的区别：

```cpp
class Example {
private:
    int x;
    int y;
    const int max_value;  // const 成员
    int& ref;             // 引用成员

public:
    // 方式一：初始化列表（推荐）
    Example(int a, int b, int max, int& r)
        : x(a), y(b), max_value(max), ref(r) {
        // 构造函数体可以为空
    }

    // 方式二：构造函数体内赋值（不推荐，而且对 const/引用成员根本不可行）
    // Example(int a, int b, int max, int& r) {
    //     x = a;
    //     y = b;
    //     max_value = max;  // 编译错误！const 成员不能赋值
    //     ref = r;          // 编译错误！引用必须在初始化时绑定
    // }
};
```

初始化列表的核心优势在于**性能和语义的正确性**。对于像 `int` 这样的基本类型，两种方式的性能差异微乎其微。但对于复杂的类类型成员，使用初始化列表可以避免一次默认构造加一次赋值操作——直接用目标值构造，省去了中间步骤。

更重要的是，**`const` 成员和引用成员只能通过初始化列表来初始化**，因为在构造函数体执行时，它们已经被默认构造完毕了——而 `const` 对象不能被二次赋值，引用不能被重新绑定。所以如果你有这两种类型的成员，初始化列表不是"推荐"，而是**唯一合法的选择**。

### 3.2 初始化列表的嵌入式应用

在嵌入式开发中，初始化列表还有一个非常实用的应用：在对象构造时直接配置硬件参数。

```cpp
class PWMChannel {
private:
    int channel;
    int frequency;

public:
    PWMChannel(int ch, int freq)
        : channel(ch), frequency(freq) {
        // 配置硬件定时器
        configure_timer(channel, frequency);
    }
};
```

初始化顺序有一个需要注意的细节：**成员变量的初始化顺序取决于它们在类定义中的声明顺序，而不是初始化列表中的书写顺序**。如果你在初始化列表里写了 `: b(a), a(10)`，编译器会先初始化 `a`（因为它先声明），再初始化 `b`——所以 `b(a)` 确实能拿到正确的 `a` 的值。但如果你的声明顺序是 `b` 在前、`a` 在后，那 `b(a)` 在初始化时 `a` 还没被初始化，读到的就是未定义的值。大多数编译器会在初始化列表顺序和声明顺序不一致时给出警告，但最好还是养成让两者保持一致的习惯。

## 4. this 指针

### 4.1 this 是什么

每一个非静态成员函数，在底层都有一个隐藏的参数——指向调用该函数的对象的指针。这个指针就是 `this`。换句话说，当你写：

```cpp
led.on();
```

编译器实际上把它翻译成了类似这样的调用（伪代码）：

```cpp
LED::on(&led);  // 把 led 的地址作为 this 指针传入
```

在成员函数内部，`this` 指向当前对象。你可以通过 `this` 来访问成员变量和成员函数。大多数情况下，你不需要显式写出 `this`——编译器会自动把"裸"的成员名解析为 `this->成员名`。但在某些场景下，显式使用 `this` 是必须的或者有帮助的。

最常见的情况是**参数名和成员变量名冲突**：

```cpp
class Sensor {
private:
    int pin;

public:
    Sensor(int pin) : pin(pin) {}  // 初始化列表中，前面的 pin 是成员，后面的 pin 是参数

    void set_pin(int pin) {
        this->pin = pin;  // this->pin 是成员变量，pin 是参数
    }
};
```

### 4.2 链式方法调用

`this` 指针的另一个常见应用是实现链式调用。方法很简单：成员函数返回 `*this` 的引用，这样调用者就可以在一行代码里连续调用多个方法。

```cpp
class StringBuilder {
private:
    char buffer[256];
    size_t length;

public:
    StringBuilder() : length(0) {
        buffer[0] = '\0';
    }

    StringBuilder& append(const char* str) {
        while (*str && length < 255) {
            buffer[length++] = *str++;
        }
        buffer[length] = '\0';
        return *this;  // 返回自身的引用
    }

    StringBuilder& append_char(char c) {
        if (length < 255) {
            buffer[length++] = c;
            buffer[length] = '\0';
        }
        return *this;
    }

    const char* c_str() const {
        return buffer;
    }
};

// 链式调用
StringBuilder sb;
sb.append("Hello").append(", ").append("World!").append_char('\n');
printf("%s", sb.c_str());
```

这种模式在嵌入式开发中特别适合用来构建配置接口或日志输出——每个调用都返回自身，写起来紧凑，读起来也流畅。

对比 C 中的做法，链式调用的底层原理其实和 C 里"函数返回结构体指针"是一样的。区别在于 C++ 通过 `this` 和引用让语法更加自然，不需要到处写 `->` 和取地址符。

## 5. 静态成员

### 5.1 静态成员变量

静态成员变量属于**类本身**，而不是某个具体的对象。这意味着：无论你创建了多少个类的实例，静态成员变量在内存中只有一份拷贝。

这在嵌入式开发中非常实用。比如，你想跟踪某个外设驱动当前有多少个实例在使用：

```cpp
class UARTPort {
private:
    int port_number;
    static int active_count;  // 声明静态成员

public:
    UARTPort(int port) : port_number(port) {
        active_count++;
    }

    ~UARTPort() {
        active_count--;
    }

    static int get_active_count() {
        return active_count;
    }
};

// 静态成员必须在类外定义（C++17 前的规则）
int UARTPort::active_count = 0;
```

注意一个容易踩坑的细节：**静态成员变量必须在类外进行定义和初始化**（C++17 引入了 `inline static` 成员可以在类内直接初始化，但 C++98 不支持这一点）。如果你只在类内声明了 `static int active_count;` 而忘了在 `.cpp` 文件里写 `int UARTPort::active_count = 0;`，链接器会报一个"undefined reference"的错误，而且这个错误往往不太好定位——因为编译是能通过的，只有链接才报错。

### 5.2 静态成员函数

静态成员函数也属于类本身，而不是某个具体对象。因此，静态成员函数**没有 `this` 指针**，这也意味着它不能访问非静态成员变量和非静态成员函数——因为这些都需要通过 `this` 来定位到具体的对象实例。

```cpp
class UARTPort {
private:
    int port_number;
    static bool hal_initialized;

public:
    static void init_hal() {
        // 初始化硬件抽象层
        hal_initialized = true;
        // port_number = 1;  // 编译错误！静态函数不能访问非静态成员
    }

    static bool is_hal_ready() {
        return hal_initialized;
    }
};
```

调用静态成员函数时，使用 `类名::函数名()` 的方式，不需要先创建对象：

```cpp
UARTPort::init_hal();
if (UARTPort::is_hal_ready()) {
    UARTPort uart(1, 115200);
}
```

这种"先检查硬件是否就绪、再创建实例"的模式在嵌入式开发中非常常见，静态成员函数正好提供了这种"和类相关、但不需要实例"的调用能力。

## 6. const 成员函数

### 6.1 const 成员函数的语义

`const` 成员函数是 C++ 提供的一种非常强的语义承诺：**这个函数不会修改对象的状态**。声明方式是在函数参数列表后加上 `const` 关键字：

```cpp
class LED {
private:
    int pin;
    bool state;

public:
    bool is_on() const {  // const 成员函数
        return state;      // 可以读取成员变量
        // state = true;   // 编译错误！不能修改成员变量
    }
};
```

这不仅仅是给读代码的人看的，更是给编译器看的。编译器会在编译期检查 `const` 成员函数内是否有任何修改成员变量的操作，一旦发现就直接报错。更重要的是，`const` 成员函数是**唯一可以被 `const` 对象调用的成员函数**：

```cpp
void print_status(const LED& led) {
    led.is_on();   // OK，is_on() 是 const 的
    // led.on();   // 编译错误！on() 不是 const 的，不能通过 const 引用调用
}
```

### 6.2 const 正确性的级联效应

`const` 正确性有一个非常重要的特点——它会"传染"。如果你的函数声明了一个 `const` 引用参数，那么通过这个引用只能调用 `const` 成员函数。而那些 `const` 成员函数如果返回了对其他对象的引用，那些引用也应该是 `const` 的。这种级联效应看起来可能有点烦，但它实际上帮你建立了一个非常强的"只读安全网"。

我们来看一个嵌入式场景中的实际例子——一个带缓存的传感器读取类：

```cpp
class TemperatureSensor {
private:
    int pin;
    mutable float cached_value;    // mutable 允许在 const 函数中修改
    mutable bool cache_valid;

public:
    TemperatureSensor(int p) : pin(p), cached_value(0), cache_valid(false) {}

    // 非 const：强制重新从硬件读取
    float read() {
        cached_value = read_from_hardware();
        cache_valid = true;
        return cached_value;
    }

    // const：优先返回缓存值
    float read_cached() const {
        if (!cache_valid) {
            // cache_valid = true;  // 如果没有 mutable，这里会编译错误
            cached_value = read_from_hardware();
            cache_valid = true;
        }
        return cached_value;
    }

    float get_cached() const {
        return cached_value;
    }

private:
    float read_from_hardware() const {
        // 实际读取 ADC
        return 25.0f;
    }
};

// 使用
void report_temperature(const TemperatureSensor& sensor) {
    // sensor.read();          // 编译错误！read() 不是 const 的
    float temp = sensor.read_cached();  // OK
    printf("Temperature: %.1f C\n", temp);
}
```

这个例子展示了一个非常实用的设计模式：提供一个非 `const` 的"强制刷新"接口和一个 `const` 的"有缓存就返回缓存"接口。调用者根据自己手里拿的是 `const` 引用还是非 `const` 引用，自动获得不同的行为保证。

### 6.3 一个实用的经验法则

在 C++ 中有一个被广泛认可的编程准则：**所有不会修改对象状态的成员函数，都应该声明为 `const`**。这不是强制的，但如果你不这么做，你的类在别人使用的时候会遇到各种"明明能读为什么编译器不让"的困扰——因为别人可能通过 `const` 引用来持有你的对象（比如作为函数参数传递），此时就只能调用 `const` 成员函数。

如果你在设计一个类的时候，某个成员函数"看起来应该只是读取数据"，但你忘了加 `const`，那你的用户就会在把对象传给接受 `const` 引用的函数时，发现调用不了这个"明明只是读取"的函数。这种错误特别阴险，因为原因不在调用处，而在类的定义处——而且错误信息往往只是一句"discards qualifiers"，新手看到完全不知道在说什么。

笔者的建议是：**养成一个习惯——写完每个成员函数后，问自己一句"这个函数需要修改对象吗？"如果答案是不需要，立刻加上 `const`。**

## 7. 友元 (friend)

### 7.1 友元是什么

友元 (friend) 是 C++ 提供的一种机制，允许你主动**打破封装边界**——让某个外部函数或外部类访问当前类的 `private` 和 `protected` 成员。

```cpp
class SensorData {
private:
    float raw_values[100];
    int count;

public:
    SensorData() : count(0) {}

    // 声明 serialize 为友元函数
    friend void serialize(const SensorData& data, uint8_t* buffer);
};

// 友元函数可以直接访问 private 成员
void serialize(const SensorData& data, uint8_t* buffer) {
    memcpy(buffer, data.raw_values, data.count * sizeof(float));
    // 这里直接访问了 raw_values 和 count，它们是 private 的
    // 但因为 serialize 被声明为友元，所以编译器允许
}
```

### 7.2 友元的危险性

友元的存在本身并不邪恶，但它几乎总是一个**危险信号**。友元意味着你主动暴露了类的内部实现细节给外部代码。从设计的角度看，这破坏了封装——而封装正是类的核心价值之一。

大多数需要友元的场景，其实可以通过更好的设计来避免。比如上面的序列化例子，完全可以通过提供一个 `const` 的公共访问接口来实现，而不需要把整个内部数组暴露出去：

```cpp
class SensorData {
private:
    float raw_values[100];
    int count;

public:
    // 提供只读访问接口，不需要友元
    const float* data() const { return raw_values; }
    int size() const { return count; }
};

void serialize(const SensorData& data, uint8_t* buffer) {
    memcpy(buffer, data.data(), data.size() * sizeof(float));
}
```

这种设计明显更安全——`SensorData` 只暴露了一个只读的指针和大小，外部代码无法修改内部数据。友元版本则把整个 `raw_values` 数组暴露给了 `serialize` 函数，如果 `serialize` 的实现有 bug，它可能会越界写入。

所以笔者的建议是：**如果一个类需要大量友元才能工作，那它大概率不该被设计成一个类**。友元应该是一种最后的手段，而不是常规手段。当你的第一反应是"加个友元"的时候，先停下来想一想：有没有不破坏封装的替代方案？

## 8. explicit 关键字

### 8.1 隐式转换的问题

C++ 允许构造函数进行隐式类型转换。也就是说，如果你有一个接受单个参数的构造函数，编译器会在需要的时候自动调用这个构造函数，把参数类型"悄悄"转换成类类型。

```cpp
class PWMChannel {
private:
    int channel;

public:
    // 没有 explicit：允许隐式转换
    PWMChannel(int ch) : channel(ch) {}
};

void set_active(PWMChannel ch) {
    // 设置某个通道为活跃
}

set_active(3);  // OK：3 被隐式转换为 PWMChannel(3)
```

这段代码能编译通过，但 `set_active(3)` 这个调用在语义上是模糊的——你传入的是一个 `int`，但函数期望的是一个 `PWMChannel` 对象。编译器"好心"帮你做了转换，但这种"好心"在大型项目中往往是灾难的来源：你可能在某个地方写错了参数类型，但编译器不但不报错，反而帮你做了一个你完全没预料到的转换，然后程序以一种莫名其妙的方式运行。

### 8.2 explicit 的作用

`explicit` 关键字用来禁止这种隐式转换。加上它之后，构造函数只能在显式调用时使用：

```cpp
class SafePWMChannel {
private:
    int channel;

public:
    explicit SafePWMChannel(int ch) : channel(ch) {}
};

void set_active(SafePWMChannel ch);

// set_active(3);                      // 编译错误！不能隐式转换
set_active(SafePWMChannel(3));         // OK：显式构造
set_active((SafePWMChannel)3);         // OK：显式转换（C 风格，不推荐）
```

笔者的建议是：**所有单参数构造函数都应该加 `explicit`，除非你非常明确地需要隐式转换**。这是一个几乎零成本的防御措施，可以避免大量由隐式转换引起的 bug。而且 `explicit` 只影响构造函数的隐式调用——显式调用完全不受影响，所以它不会限制任何你真正需要的功能。

## 9. mutable 关键字

### 9.1 mutable 的作用

`mutable` 关键字允许在 `const` 成员函数中修改被标记为 `mutable` 的成员变量。这听起来像是在违反 `const` 的承诺，但实际上有完全合理的应用场景。

我们前面在讲 `const` 成员函数的时候已经见过一个缓存示例。这里再看一个更完整的版本：

```cpp
class Sensor {
private:
    int pin;
    mutable float cached_value;   // mutable：允许 const 函数修改
    mutable bool cache_valid;
    mutable int read_count;       // 统计读取次数

public:
    explicit Sensor(int p)
        : pin(p), cached_value(0), cache_valid(false), read_count(0) {}

    float read() const {
        read_count++;              // OK：read_count 是 mutable 的
        if (!cache_valid) {
            cached_value = read_from_hardware();
            cache_valid = true;
        }
        return cached_value;
    }

    int get_read_count() const {
        return read_count;
    }

private:
    float read_from_hardware() const {
        // 实际读取硬件
        return 25.0f;
    }
};
```

在这个例子中，`read()` 函数被声明为 `const` 的，因为它对外部的承诺是"不会改变传感器的逻辑状态"——从使用者的角度看，调用 `read()` 前后传感器并没有发生任何变化。但在内部，`read()` 确实修改了缓存和计数器——这些属于**实现细节**，而不是逻辑状态的一部分。

### 9.2 什么时候用 mutable

`mutable` 适用的场景非常明确：**那些属于实现细节、不影响对象逻辑状态的成员变量**。典型的场景包括缓存、延迟计算、调试计数器、互斥锁等。

但 `mutable` 也容易被滥用。如果你发现自己在 `const` 函数里频繁修改 `mutable` 成员，而且这些修改会影响到对象的"可观测行为"，那大概率是你的 `const` 设计有问题——要么这个函数不应该是 `const` 的，要么这些成员不应该是 `mutable` 的。

一个简单的判断标准是：**如果去掉 `mutable` 标记和相关的修改代码，函数对外部行为是否完全一样？**如果答案为"是"，那 `mutable` 就是合理的；如果为"否"，那就需要重新审视设计了。

## 小结

这一章我们深入剖析了 C++ 类与对象的核心机制。从 C 结构体出发，我们看到了 `class` 如何通过访问控制把数据和操作绑在一起；构造函数和析构函数保证了对象的"获取即初始化"和"离开即清理"；成员初始化列表是性能和语义正确性的双重保障；`this` 指针解释了成员函数为什么能"知道"自己操作的是哪个对象；静态成员提供了类级别的共享状态；`const` 成员函数建立了"只读"的强契约；友元、`explicit` 和 `mutable` 则是三个"精确控制"的工具，各有其适用场景和使用边界。

在下一篇中，我们将把单个类的概念扩展到类型层次——看看 C++ 如何通过继承和多态来组织多个类之间的关系。
