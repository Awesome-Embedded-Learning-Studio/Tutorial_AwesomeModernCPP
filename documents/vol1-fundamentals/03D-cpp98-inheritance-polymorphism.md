---
title: "C++98面向对象：继承与多态"
description: "从单一类到类型层次——继承表达'是什么'关系、虚函数实现运行时多态、抽象类定义能力契约、虚析构函数保障安全释放"
chapter: 0
order: 3
tags:
  - cpp-modern
  - host
  - beginner
  - 入门
  - 基础
difficulty: beginner
reading_time_minutes: 25
prerequisites:
  - "C++98面向对象：类与对象深度剖析"
related:
  - "C++98运算符重载"
  - "何时用C++、用哪些C++特性"
cpp_standard: [11, 14, 17, 20]
platform: host
---

# C++98面向对象：继承与多态

> 完整的仓库地址在 [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP) 中，您也可以光顾一下，喜欢的话给一个 Star 激励一下作者

上一篇我们深入学习了类与对象的核心机制。现在，我们把视野从"单个类"扩展到"类与类之间的关系"——C++ 如何通过继承来表达"是什么"的语义，又如何通过多态来实现"同一接口、不同行为"。

继承和多态是面向对象编程中**最容易被滥用、也最容易被误解**的两个特性。很多初学者一提到继承，脑子里立刻浮现的是"代码复用""少写代码"，但在工程实践中，继承真正解决的问题并不是少写几行代码，而是**表达类型之间的语义关系**。多态则更进一步，它允许你通过统一的接口去操作不同类型的对象，而具体的行为在运行时才确定。

## 1. 继承 (Inheritance)

### 1.1 继承的本质：表达"是什么"关系

继承的核心是表达一种非常明确的关系：**派生类 is-a 基类**。例如，一个温度传感器"是一种传感器"，UART "是一种通信接口"。在这种语义成立的前提下，继承才是自然的。

我要强调一些事情：特别是在比较关键的设计场景下——**使用正确的语义总是比为了图省事强！使用正确的语义总是比为了图省事强！使用正确的语义总是比为了图省事强！**你也不想给未来的你和你的同事加班擦屁股吧。

我们来看一个完整的传感器层次结构示例：

```cpp
// 基类：所有传感器的共同接口
class SensorBase {
protected:
    int sensor_id;
    bool initialized;

public:
    explicit SensorBase(int id) : sensor_id(id), initialized(false) {}

    virtual ~SensorBase() {}  // 虚析构函数，后面会详细讲

    bool is_initialized() const {
        return initialized;
    }

    int get_id() const {
        return sensor_id;
    }
};

// 派生类：温度传感器
class TemperatureSensor : public SensorBase {
private:
    float offset;  // 温度校准偏移

public:
    TemperatureSensor(int id, float cal_offset = 0.0f)
        : SensorBase(id), offset(cal_offset) {}

    bool init() {
        // 温度传感器特有的初始化
        initialized = true;
        return true;
    }

    float read_celsius() {
        float raw = read_adc();
        return raw + offset;
    }

private:
    float read_adc() {
        // 实际读取 ADC 值
        return 25.0f;
    }
};

// 派生类：压力传感器
class PressureSensor : public SensorBase {
private:
    float altitude_offset;

public:
    PressureSensor(int id, float alt_offset = 0.0f)
        : SensorBase(id), altitude_offset(alt_offset) {}

    bool init() {
        // 压力传感器特有的初始化
        initialized = true;
        return true;
    }

    float read_hpa() {
        float raw = read_adc();
        return raw * 10.0f + altitude_offset;
    }

private:
    float read_adc() {
        // 实际读取 ADC 值
        return 101.325f;
    }
};
```

在这个设计中，`SensorBase` 负责定义"所有传感器都具备的能力和状态"——ID、是否已初始化等。而派生类只需要关心自己特有的行为。基类中的 `protected` 成员正是为这种场景准备的：它们不对外暴露，但允许派生类在合理范围内使用这些内部状态。

### 1.2 构造和析构顺序

当创建一个派生类对象时，构造的顺序是**从基类到派生类**——先构造基类子对象，再构造派生类自己的成员。析构的顺序则正好反过来——**从派生类到基类**。这个顺序非常合理：派生类的构造函数可能依赖基类成员已经处于合法状态，而析构时派生类必须先清理自己的资源，然后才能安全地析构基类。

```cpp
class Base {
public:
    Base() { printf("Base constructed\n"); }
    ~Base() { printf("Base destroyed\n"); }
};

class Derived : public Base {
public:
    Derived() { printf("Derived constructed\n"); }
    ~Derived() { printf("Derived destroyed\n"); }
};

// 创建和销毁
{
    Derived d;
    // 输出：
    // Base constructed
    // Derived constructed
}
// 离开作用域，输出：
// Derived destroyed
// Base destroyed
```

在派生类的构造函数中，你需要通过初始化列表来指定调用基类的哪个构造函数。如果你不指定，编译器会调用基类的默认构造函数。如果基类没有默认构造函数——比如基类只定义了一个带参数的构造函数——那你必须在派生类的初始化列表里显式调用它：

```cpp
class TemperatureSensor : public SensorBase {
public:
    TemperatureSensor(int id)
        : SensorBase(id) {  // 必须显式调用基类构造函数
        // ...
    }
};
```

### 1.3 继承的访问控制

继承方式本身也有访问控制之分，但这个话题经常让人困惑。C++ 支持三种继承方式：

- **公有继承 (`public`)**：基类的 `public` 成员在派生类中仍然是 `public` 的，`protected` 仍然是 `protected` 的。这是最常用的继承方式，维持了 "is-a" 语义。
- **保护继承 (`protected`)**：基类的 `public` 和 `protected` 成员在派生类中都变成 `protected` 的。
- **私有继承 (`private`)**：基类的 `public` 和 `protected` 成员在派生类中都变成 `private` 的。

在嵌入式工程中，绝大多数情况下你只应该使用**公有继承**。原因很简单：公有继承才能维持 "is-a" 语义，也才能保证通过基类接口使用派生类对象是安全且直观的。`protected` 继承和 `private` 继承更多是语言层面的技巧，适用场景非常有限。

### 1.4 对象切片

在使用继承时，有一个非常容易被忽略的陷阱——**对象切片 (Object Slicing)**。当你用一个派生类对象去初始化或赋值给一个基类对象（不是指针或引用）时，派生类特有的部分会被"切掉"：

```cpp
TemperatureSensor temp(1);
SensorBase base = temp;  // 对象切片！

// base 现在是一个 SensorBase 对象
// TemperatureSensor 特有的成员（offset, read_celsius()）全部丢失
```

对象切片发生的原因很简单：`base` 是一个 `SensorBase` 类型的变量，它的内存空间只够容纳 `SensorBase` 的成员。当你把 `temp` 赋给它时，编译器只拷贝了 `SensorBase` 的部分，其余的被丢弃了。

避免对象切片的方法也很简单：**使用引用或指针，而不是直接用值类型**。通过基类引用或指针来操作派生类对象，不会发生切片：

```cpp
TemperatureSensor temp(1);
SensorBase& ref = temp;   // OK：引用，不会切片
SensorBase* ptr = &temp;  // OK：指针，不会切片
```

### 1.5 多重继承与菱形继承

多重继承允许一个类同时继承多个基类。在某些场景下这很自然——比如一个设备同时具有"可读"和"可写"两种能力：

```cpp
class Readable {
public:
    virtual int read() = 0;
};

class Writable {
public:
    virtual void write(int value) = 0;
};

class SerialPort : public Readable, public Writable {
private:
    int buffer;

public:
    int read() override {
        return buffer;
    }

    void write(int value) override {
        buffer = value;
    }
};
```

这种"接口继承"式的多重继承相对安全。但多重继承的真正麻烦在于**菱形继承**——当两个基类又继承自同一个共同基类时：

```cpp
class Base {
public:
    int value;
};

class Derived1 : public Base { };
class Derived2 : public Base { };

class Multiple : public Derived1, public Derived2 {
    void foo() {
        // value 是歧义的：是 Derived1::value 还是 Derived2::value？
    }
};
```

此时 `Multiple` 对象内部包含**两份** `Base` 子对象——一份来自 `Derived1`，一份来自 `Derived2`。访问 `value` 时编译器不知道你要的是哪一份，直接报歧义错误。

C++ 提供了**虚继承**来解决这个问题：

```cpp
class Derived1 : virtual public Base { };
class Derived2 : virtual public Base { };

class Multiple : public Derived1, public Derived2 {
    void foo() {
        value = 10;  // 现在只有一份 Base，不再有歧义
    }
};
```

虚继承确保无论 `Base` 在继承链中被间接继承了多少次，最终对象中只包含一份 `Base` 子对象。但虚继承的代价是：对象布局更复杂、构造函数的调用规则更晦涩、运行时可能多一次间接寻址。在嵌入式环境下，这种复杂性通常是不值得的。

一个相对安全的共识是：**多重继承只用于"接口继承"（基类全是纯虚函数），而不要用于"实现继承"**。如果你的多重继承基类中包含了数据成员或具体实现，那大概率已经走在了一条复杂的路上。

## 2. 多态 (Polymorphism)

### 2.1 什么是多态

如果说继承回答的是"你是什么"，那么多态回答的就是"你现在表现得像什么"。多态允许你通过基类指针或引用去操作一个派生类对象，并在运行时调用到派生类的实现。

这种能力的核心在于**虚函数 (virtual function)**。当一个成员函数被声明为 `virtual`，就意味着：**具体调用哪一个实现，要等到运行时才能确定，而不是在编译期静态绑定**。这正是多态能够成立的根本原因。

我们先看一个最基本的例子：

```cpp
class Animal {
public:
    virtual void speak() {  // 虚函数
        printf("...\n");
    }

    virtual ~Animal() {}  // 虚析构函数
};

class Dog : public Animal {
public:
    void speak() override {
        printf("Woof!\n");
    }
};

class Cat : public Animal {
public:
    void speak() override {
        printf("Meow!\n");
    }
};
```

现在我们可以通过基类指针来调用 `speak()`，而具体的行为取决于指针实际指向的对象类型：

```cpp
void make_sound(Animal* animal) {
    animal->speak();  // 运行时决定调用哪个版本
}

Dog dog;
Cat cat;
make_sound(&dog);  // 输出 "Woof!"
make_sound(&cat);  // 输出 "Meow!"
```

这个例子虽然简单，但已经展示了多态的核心价值：`make_sound` 函数完全不知道也不需要知道 `Animal` 的具体子类型是什么。它只需要知道"这个东西会 `speak()`"。这种**调用者不依赖具体类型、只依赖抽象接口**的能力，是大型系统架构的基石。

### 2.2 虚函数的底层机制：虚表 (vtable)

理解多态的底层机制，有助于我们在嵌入式场景中做出正确的工程判断。这里我们做一个简要的介绍。

当你在一个类中声明了虚函数（或继承了虚函数），编译器会为这个类生成一张**虚函数表 (virtual table, 简称 vtable)**。这张表是一个函数指针数组，每个条目对应一个虚函数，存储着该类对这个虚函数的实际实现地址。

同时，每个包含虚函数的对象，在内存布局中都会多出一个隐藏的指针——**虚表指针 (vptr)**，指向该对象所属类的 vtable。

当调用 `animal->speak()` 时，编译器生成的代码大致做了这几件事：

1. 通过 `animal` 指针找到对象的内存起始位置
2. 从对象中取出 `vptr`，找到对应的 vtable
3. 在 vtable 中查找 `speak()` 对应的条目
4. 通过函数指针发起间接调用

这就是为什么虚函数调用比普通函数调用多了一层间接性的原因——它需要在运行时通过 vtable 来查找实际应该调用的函数。**这个"间接跳转"就是多态的全部运行时开销。**

在 PC 上，一次间接跳转的开销微乎其微——可能就是多访问一次缓存。但在资源紧张、对实时性敏感的嵌入式系统中，这种开销就需要被认真对待了。具体来说：

- **代码大小**：每个含虚函数的类都有一张 vtable，这会占用 Flash 空间
- **对象大小**：每个对象多了一个 `vptr`（通常是一个指针的大小，4 或 8 字节），这在 RAM 紧张的 MCU 上可能是有意义的
- **调用开销**：一次间接跳转，可能会影响流水线和分支预测

因此，一个非常重要的工程判断是：**只有当"解耦带来的收益"明确大于"运行时开销和复杂度"时，多态才值得使用**。

### 2.3 纯虚函数与抽象类

纯虚函数是一种特殊的虚函数——它在基类中没有实现，要求所有派生类必须提供自己的实现。包含至少一个纯虚函数的类被称为**抽象类**，它不能被直接实例化。

```cpp
// 抽象类：通信接口
class CommunicationInterface {
public:
    virtual ~CommunicationInterface() = default;

    virtual bool send(const uint8_t* data, size_t length) = 0;
    virtual size_t receive(uint8_t* buffer, size_t max_length) = 0;
    virtual bool is_connected() const = 0;
};
```

抽象类不是用来创建对象的，而是用来**定义一种能力契约**。派生类必须完整实现所有纯虚函数，才能成为"合法的具体类型"：

```cpp
class UARTDriver : public CommunicationInterface {
private:
    int port;
    int baudrate;

public:
    UARTDriver(int p, int baud) : port(p), baudrate(baud) {}

    bool send(const uint8_t* data, size_t length) override {
        // UART 特定的发送实现
        for (size_t i = 0; i < length; ++i) {
            uart_write_byte(port, data[i]);
        }
        return true;
    }

    size_t receive(uint8_t* buffer, size_t max_length) override {
        // UART 特定的接收实现
        size_t count = 0;
        while (count < max_length && uart_has_data(port)) {
            buffer[count++] = uart_read_byte(port);
        }
        return count;
    }

    bool is_connected() const override {
        return true;  // UART 是有线连接，默认始终连接
    }
};

class SPIDriver : public CommunicationInterface {
private:
    int cs_pin;

public:
    explicit SPIDriver(int cs) : cs_pin(cs) {}

    bool send(const uint8_t* data, size_t length) override {
        gpio_write(cs_pin, LOW);  // 拉低 CS
        spi_transfer(data, length);
        gpio_write(cs_pin, HIGH); // 拉高 CS
        return true;
    }

    size_t receive(uint8_t* buffer, size_t max_length) override {
        gpio_write(cs_pin, LOW);
        size_t count = spi_read(buffer, max_length);
        gpio_write(cs_pin, HIGH);
        return count;
    }

    bool is_connected() const override {
        return gpio_read(cs_pin) == LOW;  // 简单判断
    }
};
```

现在，上层协议处理逻辑可以完全不关心底层是 UART 还是 SPI：

```cpp
void send_command(CommunicationInterface& comm, const uint8_t* cmd, size_t len) {
    comm.send(cmd, len);
}

// 使用
UARTDriver uart(1, 115200);
SPIDriver spi(5);

send_command(uart, cmd, sizeof(cmd));  // 通过 UART 发送
send_command(spi, cmd, sizeof(cmd));   // 通过 SPI 发送
```

这种设计在驱动层尤为常见。UART、SPI、I2C 看起来完全不同，但在"发送数据""接收数据"这个层面，它们可以共享一套抽象接口。上层协议处理逻辑只依赖接口，而不依赖任何具体硬件，这使得代码的可移植性和可测试性大幅提升。

### 2.4 虚析构函数

虚析构函数是多态中一个极其容易被忽视、却又极其致命的细节。

**只要你打算通过基类指针来管理派生类对象的生命周期，那么基类的析构函数就必须是虚的。**否则，在 `delete` 基类指针时，只会调用基类的析构函数，派生类中持有的资源将完全得不到释放。

```cpp
class BadBase {
public:
    ~BadBase() { printf("BadBase destroyed\n"); }  // 非虚析构函数
};

class BadDerived : public BadBase {
private:
    int* data;

public:
    BadDerived() : data(new int[100]) {}
    ~BadDerived() {
        delete[] data;
        printf("BadDerived destroyed\n");
    }
};

// 使用
BadBase* ptr = new BadDerived();
delete ptr;  // 只调用 ~BadBase()，~BadDerived() 被跳过！
// 输出只有 "BadBase destroyed"
// data 对应的 400 字节内存泄漏了！
```

加上 `virtual` 之后：

```cpp
class GoodBase {
public:
    virtual ~GoodBase() { printf("GoodBase destroyed\n"); }
};

class GoodDerived : public GoodBase {
private:
    int* data;

public:
    GoodDerived() : data(new int[100]) {}
    ~GoodDerived() {
        delete[] data;
        printf("GoodDerived destroyed\n");
    }
};

GoodBase* ptr = new GoodDerived();
delete ptr;
// 输出：
// GoodDerived destroyed
// GoodBase destroyed
// 内存正确释放
```

一个简单但几乎可以写成铁律的经验是：**只要类中存在任何虚函数，就一定要把析构函数也声明为 virtual**。这不费什么事，但可以避免一类在嵌入式中表现为"莫名其妙的内存泄漏"或"外设状态异常"、而且定位起来异常困难的问题。

### 2.5 嵌入式中何时使用多态

在嵌入式实际工程中，多态最有价值的应用场景，往往出现在"驱动抽象"和"协议解耦"上。但也并不是所有场景都适合用多态。

**适合用多态的场景**：系统需要支持多种硬件变体（比如同时兼容 UART 和 SPI 通信的传感器驱动）；或者需要在不同平台间移植时把平台相关的代码隔离到具体实现类中；又或者你想在不修改现有代码的前提下，通过新增派生类来扩展系统行为。

**不适合用多态的场景**：系统只有一种确定不变的硬件配置；对象数量非常多（每个对象都要多一个 vptr，在 RAM 只有几 KB 的 MCU 上可能承受不起）；或者对实时性有极端要求（虚函数调用的间接跳转虽然有开销，但关键是不确定性——你无法在编译期确定调用的目标地址，这对某些硬实时系统是不可接受的）。

笔者的建议是：在嵌入式开发中，**从不用多态开始，直到你明确感受到"需要用统一的接口来操作不同的实现"这个需求**。不要为了"代码看起来更 OOP"而引入多态——这是典型的过度设计。

## 小结

这一章我们学习了继承和多态——C++ 面向对象体系中最核心的两个机制。继承用于表达"是什么"的语义关系，公有继承是压倒性的首选方案。多态通过虚函数实现了运行时的行为分发，让我们可以通过统一的基类接口操作不同的派生类对象。虚析构函数是使用多态时的安全底线，忘掉它的后果是资源泄漏。

继承和多态都是强大的工具，但它们也引入了更复杂的对象关系、更难追踪的调用路径和额外的运行时开销。在嵌入式开发中，判断是否使用它们的标准非常简单：**解耦带来的收益是否明确大于引入的复杂度和开销**。

在下一篇中，我们将学习运算符重载——让自定义类型像内置类型一样参与表达式计算的能力。
