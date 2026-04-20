---
title: "抽象类与接口"
description: "掌握纯虚函数、抽象类的设计方法，学会用接口隔离原则组织类型层次"
chapter: 8
order: 3
difficulty: intermediate
reading_time_minutes: 12
platform: host
prerequisites:
  - "虚函数与多态"
tags:
  - cpp-modern
  - host
  - intermediate
  - 进阶
cpp_standard: [11, 14, 17, 20]
---

# 抽象类与接口

上一章我们把虚函数和多态的机制彻底拆了一遍，知道了通过基类指针调用 `speak()` 时，编译器会怎么查 vtable、怎么在运行时找到真正的函数地址。但有一个问题我们刻意绕过了——如果基类本身就不该被实例化呢？比如我们定义了一个 `Shape` 类来表示"形状"，但"形状"本身是个抽象概念，世界上不存在一个"既不是圆、也不是矩形、也不是任何具体形状"的 Shape 对象。它只是一个公共接口，真正有意义的都是它的派生类。

这就是抽象类要解决的问题。这一章我们从纯虚函数讲起，搞清楚 C++ 中的抽象类机制，然后讨论接口设计——特别是 C++ 没有 `interface` 关键字这件事该怎么处理，以及接口隔离原则在实际工程中怎么落地。

> **踩坑预警**：如果你习惯从 Java 或 C# 转过来，可能会下意识地以为 C++ 的抽象类和 Java 的 `abstract class` 完全等价。大部分情况下确实如此，但 C++ 的纯虚函数有一个 Java 不具备的特性——纯虚函数可以拥有默认实现。这个差异后面会专门讲，先别急。

## 第一步——纯虚函数与抽象类的诞生

想让一个类变成"不可实例化"的抽象类，只需要在类中声明至少一个**纯虚函数**。语法很简单，在虚函数声明的末尾加上 `= 0`：

```cpp
class Shape {
public:
    virtual ~Shape() = default;
    virtual double area() const = 0;       // 纯虚函数
    virtual const char* name() const = 0;   // 纯虚函数
};
```

`= 0` 这个写法看起来有点怪，但它的语义很明确：这个函数在基类中没有实现，派生类**必须**提供自己的版本。包含至少一个纯虚函数的类就是**抽象类**，编译器会阻止你直接创建它的对象：

```cpp
Shape s;            // 编译错误！不能实例化抽象类
Shape* p = nullptr; // OK，指针和引用是可以的
```

通过指针或引用来操作派生类对象完全没问题，这正是多态发挥作用的前提。派生类要想变成"具体类"（可以实例化的类），就必须把基类中的每一个纯虚函数都实现一遍，一个都不能落下：

```cpp
class Circle : public Shape {
    double radius_;
public:
    explicit Circle(double r) : radius_(r) {}
    double area() const override { return 3.14159265 * radius_ * radius_; }
    const char* name() const override { return "Circle"; }
};

class Rectangle : public Shape {
    double width_, height_;
public:
    Rectangle(double w, double h) : width_(w), height_(h) {}
    double area() const override { return width_ * height_; }
    const char* name() const override { return "Rectangle"; }
};

// 通过基类引用统一操作
void print_area(const Shape& shape) {
    std::cout << shape.name() << ": " << shape.area() << std::endl;
}

Circle c(2.0);
Rectangle r(3.0, 4.0);
print_area(c);  // Circle: 12.5664
print_area(r);  // Rectangle: 12
```

> **踩坑预警**：如果派生类忘记实现某个纯虚函数，那么派生类自己也会变成抽象类。这时候如果你尝试实例化它，编译器会报错说 "cannot instantiate abstract class"。初学者经常被这个报错搞得一脸懵，原因往往就是漏掉了一个纯虚函数没有 override。好消息是编译器的错误信息里通常会列出哪些纯虚函数还没实现，照着补上就行。

## 抽象类的设计思路

抽象类的设计思路可以用一句话概括：**基类定义"能做什么"，派生类决定"怎么做"**。

在我们上面的 `Shape` 例子中，`Shape` 说"每个形状都能计算面积、都有名字"，但具体怎么算、叫什么名字，交给 `Circle` 和 `Rectangle` 自己去定。这种分工非常清晰——基类是一份**契约**，派生类是**签约方**。任何派生类想要成为"可用的具体类型"，就必须履行契约中规定的全部义务。

再看一个更贴近实际工程的例子。假设我们正在开发一个日志系统，需要支持多种输出目标——控制台、文件、网络。我们可以定义一个抽象的 `ILogger` 来统一接口，然后 `ConsoleLogger` 直接输出到 `std::cout`，`FileLogger` 追加写入到指定文件。上层业务代码只需要依赖 `ILogger` 接口，完全不需要知道底层是写到控制台还是写到文件：

```cpp
class ILogger {
public:
    virtual ~ILogger() = default;
    virtual void log_info(const std::string& msg) = 0;
    virtual void log_warning(const std::string& msg) = 0;
    virtual void log_error(const std::string& msg) = 0;
};

class ConsoleLogger : public ILogger {
public:
    void log_info(const std::string& msg) override {
        std::cout << "[INFO] " << msg << std::endl;
    }
    void log_warning(const std::string& msg) override {
        std::cout << "[WARN] " << msg << std::endl;
    }
    void log_error(const std::string& msg) override {
        std::cout << "[ERROR] " << msg << std::endl;
    }
};
```

这就是抽象类最核心的价值——**解耦**。抽象类把"接口定义"和"具体实现"彻底分离开来，使得我们可以独立地修改其中一端而不影响另一端。未来新增一个 `NetworkLogger` 只需继承 `ILogger` 并实现三个方法，上层代码一行都不用改。

## C++ 的接口——没有 interface 关键字的世界

如果你写过 Java 或 C#，可能会觉得奇怪：C++ 怎么连个 `interface` 关键字都没有？确实没有，但 C++ 的抽象类机制完全能覆盖接口的语义。C++ 社区的惯例是：当一个类的所有成员函数都是纯虚函数，并且它没有非静态数据成员，我们就称它为一个**接口类**（interface class）。

```cpp
// 标准的 C++ 接口类
class ISerializable {
public:
    virtual ~ISerializable() = default;
    virtual std::string serialize() const = 0;
    virtual bool deserialize(const std::string& data) = 0;
};
```

我们注意几个细节。接口类的名字习惯以 `I` 开头（比如 `ISerializable`、`IComparable`、`ILogger`），这是一种广泛使用的命名约定，能让阅读者一眼就认出"这是一个纯接口"。虚析构函数是必须的——只要你的类有可能通过基类指针被 `delete`，虚析构函数就是不可妥协的底线。`= default` 是 C++11 引入的写法，比手写一个空的析构函数体更简洁，也表达了"使用编译器生成的默认实现"的语义。

## 接口隔离原则——别让派生类实现它不需要的方法

聊到接口设计，就不得不提 SOLID 原则中的 **I——接口隔离原则（Interface Segregation Principle, ISP）**。它的核心思想是：不要强迫一个类去实现它用不到的方法。

举个反面例子。假设我们定义了一个"全能"的设备接口，把连接管理、数据读写、串口配置全部混在一起：

```cpp
// 反面教材：臃肿的"胖接口"
class IDevice {
public:
    virtual ~IDevice() = default;
    virtual bool connect() = 0;
    virtual void disconnect() = 0;
    virtual int read() = 0;
    virtual void write(int value) = 0;
    virtual void flush() = 0;
    virtual void set_baudrate(int rate) = 0;
};
```

如果我们要实现一个只读的温度传感器，它根本不需要 `write()`、`flush()`、`set_baudrate()` 这些方法，但因为是继承自 `IDevice`，还是得全部实现一遍——哪怕只是写个空函数或者直接抛异常。更严重的问题是它会模糊类型之间的语义边界：一个只能读的传感器被迫宣称自己"会写"，调用者很容易被误导。

正确的做法是把大接口拆成若干个**小而聚焦**的接口，每个接口只描述一种能力：

```cpp
class IConnectable {
public:
    virtual ~IConnectable() = default;
    virtual bool connect() = 0;
    virtual void disconnect() = 0;
};

class IReadable {
public:
    virtual ~IReadable() = default;
    virtual int read() = 0;
};

class IWritable {
public:
    virtual ~IWritable() = default;
    virtual void write(int value) = 0;
    virtual void flush() = 0;
};
```

现在每种设备只需要继承自己真正需要的接口。一个只读的温度传感器实现 `IConnectable` 和 `IReadable` 就够了，完全不用碰 `IWritable`。而一个全双工的串口驱动可以实现所有三个接口。这正是接口隔离原则想要达成的效果：**每个类只暴露它真正支持的能力，不多也不少。**

## 纯虚函数的默认实现——一个容易被忽略的高级技巧

接下来聊一个比较少被提及、但在某些场景下非常有用的特性：**纯虚函数可以有函数体**。

没错，你没看错。一个声明为 `= 0` 的纯虚函数，依然可以在类外部提供一个默认实现：

```cpp
class Base {
public:
    virtual ~Base() = default;
    virtual void on_error(const std::string& msg) = 0;  // 纯虚函数
};

// 纯虚函数的默认实现——类外部定义
void Base::on_error(const std::string& msg) {
    std::cerr << "[ERROR] " << msg << std::endl;
}
```

这看起来有点矛盾——既然是"纯虚"的，怎么还有实现？关键在于：`= 0` 影响的是**类的抽象性质**（有没有这个实现不影响类是否为抽象类），而函数体提供的是一个**可选的默认行为**。派生类仍然必须 override 这个函数，但它可以选择在 override 的版本中显式调用基类版本来复用通用逻辑：

```cpp
class Derived : public Base {
public:
    void on_error(const std::string& msg) override {
        Base::on_error(msg);    // 先复用基类的默认行为
        write_to_log_file(msg); // 再追加自己的处理
    }
};
```

这种技术常用在框架设计中——基类通过纯虚函数强制派生类"必须处理这个事件"，同时又提供通用默认行为供按需复用。不过说实话，这种用法在日常业务代码中并不常见，这里点到为止，知道有这么回事就行。

## 实战演练——一个序列化器框架

现在我们把前面学的东西串起来，动手实现一个完整的小框架。场景是这样的：我们需要一套序列化框架，支持把数据转成 JSON 或 XML 格式。通过定义一个 `ISerializer` 接口，上层代码完全不关心底层用的什么格式。

```cpp
#include <iostream>
#include <string>

/// @brief 序列化器接口——支持不同格式的数据输出
class ISerializer {
public:
    virtual ~ISerializer() = default;
    virtual void begin_object(const std::string& name) = 0;
    virtual void end_object() = 0;
    virtual void write_field(const std::string& key,
                             const std::string& value) = 0;
    virtual std::string result() const = 0;
};
```

然后实现 JSON 和 XML 两种序列化器。它们的内部结构很不一样——JSON 需要处理花括号和引号，XML 需要处理标签对——但对外暴露的接口完全一致：

```cpp
class JSONSerializer : public ISerializer {
    std::string output_;
    int depth_ = 0;
public:
    void begin_object(const std::string& name) override {
        if (depth_ > 0) output_ += "\"" + name + "\": ";
        output_ += "{\n";
        ++depth_;
    }
    void end_object() override { --depth_; output_ += "}\n"; }
    void write_field(const std::string& key,
                     const std::string& value) override {
        output_ += "  \"" + key + "\": \"" + value + "\",\n";
    }
    std::string result() const override { return output_; }
};

class XMLSerializer : public ISerializer {
    std::string output_;
    std::string current_tag_;
public:
    void begin_object(const std::string& name) override {
        current_tag_ = name;
        output_ += "<" + name + ">\n";
    }
    void end_object() override { output_ += "</" + current_tag_ + ">\n"; }
    void write_field(const std::string& key,
                     const std::string& value) override {
        output_ += "  <" + key + ">" + value + "</" + key + ">\n";
    }
    std::string result() const override { return output_; }
};
```

上层函数通过接口引用来使用序列化器——它对 JSON 和 XML 的细节一无所知：

```cpp
void serialize_sensor_data(ISerializer& serializer) {
    serializer.begin_object("sensor");
    serializer.write_field("id", "TEMP-001");
    serializer.write_field("value", "23.5");
    serializer.write_field("unit", "celsius");
    serializer.end_object();
}

int main() {
    JSONSerializer json;
    XMLSerializer xml;
    serialize_sensor_data(json);
    serialize_sensor_data(xml);
    std::cout << "=== JSON ===\n" << json.result() << "\n";
    std::cout << "=== XML ===\n" << xml.result() << std::endl;
    return 0;
}
```

编译运行，看看输出：

```text
=== JSON ===
{
  "id": "TEMP-001",
  "value": "23.5",
  "unit": "celsius",
}

=== XML ===
<sensor>
  <id>TEMP-001</id>
  <value>23.5</value>
  <unit>celsius</unit>
</sensor>
```

`serialize_sensor_data` 函数只知道"有序列化器这个东西，我能往里写字段"。未来如果我们需要支持 YAML、Protobuf 或者任何新格式，只需要新增一个 `ISerializer` 的实现类就行，上层代码一行都不用改。这正是抽象类和接口带来的扩展性。

## 练手时间

### 练习一：设计 IComparable 接口

定义一个 `IComparable<T>` 接口模板，包含一个纯虚函数 `compare_to`。然后实现一个 `Student` 类，按学号排序。

```cpp
template <typename T>
class IComparable {
public:
    virtual ~IComparable() = default;
    /// @returns <0 表示 this < other, 0 表示相等, >0 表示 this > other
    virtual int compare_to(const T& other) const = 0;
};
```

### 练习二：插件系统框架

设计一个简单的插件框架：定义一个 `IPlugin` 接口（包含 `name()`、`version()`、`initialize()` 和 `shutdown()` 四个纯虚函数），然后实现两到三个具体的插件类。编写一个 `PluginManager`，用 `std::vector<IPlugin*>` 管理所有插件，并提供 `load_all()` 和 `unload_all()` 方法。这个练习能帮你把抽象类、接口和运行时多态综合起来用一遍。

## 小结

这一章我们围绕"基类不应该被实例化"这个需求，学习了纯虚函数和抽象类。核心要点是：在虚函数声明后加 `= 0` 就能让它变成纯虚函数，从而让包含它的类变成抽象类；派生类必须实现所有纯虚函数才能成为具体类。C++ 没有专门的 `interface` 关键字，但"全纯虚函数 + 无数据成员 + 虚析构函数"的类就是事实上的接口。接口隔离原则告诉我们，与其设计一个包罗万象的胖接口，不如拆成几个小而专注的接口，让每个类只承担它真正需要的职责。

下一章我们讨论多重继承和虚继承——当继承关系变得复杂时，C++ 提供了哪些机制来处理歧义和冗余。
