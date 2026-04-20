---
title: "引用"
description: "深入理解 C++ 引用：引用的语法、引用与指针的区别，以及 const 引用在函数参数中的重要作用"
chapter: 4
order: 3
difficulty: beginner
reading_time_minutes: 12
platform: host
prerequisites:
  - "指针运算与数组"
tags:
  - cpp-modern
  - host
  - beginner
  - 入门
  - 基础
cpp_standard: [11, 14, 17, 20]
---

# 引用

指针很强大，但说实话，也很容易惹麻烦。上一章我们花了大量篇幅和指针打交道——解引用、取地址、空指针检查、`->` 操作符......写多了你就会发现，很多场景下我们并不需要指针的全部能力。我们只是想"给函数传一个大对象但不想拷贝"，或者"让函数修改调用者的变量"。这些需求用指针当然能做，但语法上总显得笨重。C++ 给了我们一个更安全、更简洁的替代方案：**引用**。这一章我们就来把引用从头到尾搞清楚。

## 第一步——引用到底是什么

引用的本质是一个**别名**——给一个已经存在的变量起另外一个名字。就像你同事叫"张三"，大家都叫他"老张"，不管你喊哪个名字，指的都是同一个人。在底层实现上，引用通常通过指针来完成，但语言层面把指针的那些危险操作全部屏蔽掉了，留给我们的只是一个干净的"另一个名字"。

来看最基本的用法：

```cpp
int value = 42;
int& ref = value;  // ref 是 value 的别名

ref = 100;         // 通过别名修改原变量
// 现在 value 也是 100
```

`int& ref = value;` 这一行做了两件事：声明 `ref` 是一个绑定到 `int` 的引用，并且立刻把它绑定到 `value` 上。从这行代码往后，`ref` 和 `value` 就是同一个东西——对 `ref` 的任何操作，都等价于对 `value` 的操作。没有额外内存开销，没有间接寻址的语法负担，就是这么简单。

不过引用有两个非常严格的约束，理解它们是安全使用引用的前提。第一，**引用必须在声明时就初始化**。你不能先写 `int& ref;` 然后后面再让它指向某个变量——这行代码根本编译不过去。引用不像指针那样可以先设成 `nullptr` 以后再说，它从诞生的那一刻起就必须绑定到一个实实在在的对象上。第二，**引用一旦绑定就不能换目标**。这一点特别容易踩坑，我们单独拿出来看：

```cpp
int value = 42;
int& ref = value;

int other = 200;
ref = other;  // 这不是"让 ref 指向 other"！
```

`ref = other;` 这行代码的效果是——把 `other` 的值（200）赋给了 `ref` 所引用的对象（也就是 `value`）。执行完毕后 `value` 变成了 200，`ref` 依然是 `value` 的引用，和 `other` 没有任何关系。引用的绑定是**一次性**的、**不可撤销**的，之后所有的赋值操作都只是在修改被引用对象的值。

> ⚠️ **踩坑预警**
> 很多初学者看到 `ref = other;` 会误以为这是"重新绑定"。实际上 C++ 根本没有"重新绑定引用"的语法——所有对引用的赋值都是对被引用对象的赋值。如果你需要"可重新指向"的语义，那你要用的不是引用，而是指针。

## 第二步——引用与指针，到底选谁

既然引用和指针都能实现"间接操作对象"，那它们之间到底有什么区别？我们逐条对比一下：

**必须初始化 vs 可以悬空**。引用声明时必须绑定到一个对象，所以一个引用永远是"有效的"（前提是你没有搞出悬空引用这种高级 bug）。指针则可以先声明为 `nullptr`，后面再赋值，灵活但也意味着你每次使用前都得考虑"它会不会是空的"。

**不可重绑 vs 可重指向**。引用一旦绑定就终身不变，指针随时可以指向别的对象。如果你需要"迭代器式"地遍历内存、或者需要表达"可能没有对象"的语义，指针是不二之选。

**无解引用语法 vs 需要 `*` 和 `->`**。使用引用就像使用普通变量一样，直接写名字就行。指针则需要 `*ptr` 或 `ptr->member` 来访问目标，代码看起来明显更啰嗦。

**没有空引用 vs 空指针**。严格来说，C++ 中不存在"空引用"——引用必须绑定到一个有效对象。但指针可以是 `nullptr`，这既是它的灵活性所在，也是大量 bug 的来源。

用一个实际例子来感受两者的差异。假设我们有一个结构体需要在函数里修改：

```cpp
struct SensorData {
    float temperature;
    float humidity;
    float pressure;
};

// 指针版本：需要空指针检查，用 -> 访问成员
void fix_temperature(SensorData* data)
{
    if (data != nullptr) {      // 每次都得检查
        data->temperature += 0.5f;
    }
}

// 引用版本：干净利落，不需要额外检查
void fix_temperature(SensorData& data)
{
    data.temperature += 0.5f;   // 直接用 . 访问
}
```

那什么时候该用指针？笔者的建议是——**默认用引用，除非你需要引用做不到的事情**。具体来说，当你需要表达"可能没有对象"的概念时用指针（或后续会学到的 `std::optional`）；当你需要在运行时改变指向的目标时用指针；当你需要做指针算术来遍历内存时用指针。其余所有场景，引用都是更安全的选择。

> ⚠️ **踩坑预警**
> 严格来说，通过某些"非常规手段"你可以制造出一个绑定到空地址的引用，比如 `int& ref = *static_cast<int*>(nullptr);`。这行代码能编译通过，但使用 `ref` 就是未定义行为。千万别这么写——如果有人说"引用也可以为空"，那就是在钻语言规则的空子，实际工程中完全不应该出现这种代码。

## 第三步——引用作为函数参数

引用最常见的用途是作为函数参数。我们先看一个经典的例子：交换两个变量的值。在 C 里我们只能传指针：

```cpp
// C 风格：指针版本
void swap_by_pointer(int* a, int* b)
{
    int temp = *a;
    *a = *b;
    *b = temp;
}

int x = 10, y = 20;
swap_by_pointer(&x, &y);  // 调用时需要取地址
```

用引用重写，整个世界都清净了：

```cpp
// C++ 风格：引用版本
void swap_by_reference(int& a, int& b)
{
    int temp = a;
    a = b;
    b = temp;
}

int x = 10, y = 20;
swap_by_reference(x, y);  // 调用时直接传变量，不需要 &
```

函数内部不需要 `*` 解引用，调用处不需要 `&` 取地址——代码可读性上了一个台阶。标准库的 `std::swap` 也是用引用实现的，原理一模一样。

但很多时候我们传参数并不是为了修改，而是为了**避免拷贝开销**。一个包含大量数据的结构体、一个长字符串，如果按值传递就要整个复制一份，既浪费栈空间又浪费时间。这时候 `const` 引用就派上用场了：

```cpp
// 按值传递：拷贝整个 string，浪费
void print_by_value(std::string s)
{
    std::cout << s << std::endl;
}

// const 引用传递：不拷贝，不修改，完美
void print_by_ref(const std::string& s)
{
    std::cout << s << std::endl;
    // s = "hack";  // 编译错误！const 引用不允许修改
}
```

`const std::string&` 这个组合在 C++ 中出现得极其频繁，基本上是"传只读大对象"的标准范式。`const` 告诉编译器和调用者两件事：第一，这个函数不会修改传入的对象；第二，编译器会在编译期拦截任何试图修改的动作。调用者看到参数是 `const&` 就可以放心大胆地把数据交出去，不用担心被偷偷篡改。

当然，有一个实用的经验法则：对于基本类型（`int`、`double`、指针等），按值传递就行，因为拷贝开销可以忽略不计；对于任何比基本类型大的东西——`std::string`、结构体、容器——传 `const` 引用。

## 第四步——引用作为返回值

函数也可以返回引用，这在 C++ 中是一种非常实用的模式。最常见的用法是返回类成员的引用，让外部代码可以直接读写内部数据：

```cpp
class Sensor {
    float temperature_;
    float humidity_;

public:
    Sensor(float t, float h) : temperature_(t), humidity_(h) {}

    // 返回成员的引用，允许外部直接读取和修改
    float& temperature() { return temperature_; }

    // const 版本：只读访问
    const float& temperature() const { return temperature_; }
};

Sensor s(25.0f, 60.0f);
s.temperature() = 26.5f;  // 直接通过引用修改内部成员
```

返回引用的另一个经典应用是**链式调用**——让函数返回 `*this` 的引用，这样调用者就能在一行代码里串联多个操作。标准库的 `operator<<` 就是这么工作的：`std::cout << a << b << c;` 能连续输出，是因为每次 `<<` 都返回了 `std::cout` 的引用。

但返回引用有一个**致命的陷阱**——绝对不要返回局部变量的引用。局部变量存储在栈上，函数返回后栈帧就被回收了，此时引用指向的是一块已经被释放的内存：

```cpp
// 危险！返回局部变量的引用
int& dangerous()
{
    int local = 42;
    return local;  // 函数返回后 local 已销毁
    // 引用变成了悬空引用——使用它是未定义行为
}
```

这种 bug 的阴险之处在于：程序可能偶尔跑得很好，偶尔莫名其妙崩溃，而且崩溃的位置和原因毫无规律。因为那块栈内存刚好没被覆盖时，引用还能读到"正确"的值；一旦被后续函数调用覆盖了，读出来的就是垃圾数据。

> ⚠️ **踩坑预警**
> 判断返回引用是否安全的规则很简单——**被引用对象的生命周期必须长于函数调用本身**。成员变量、全局变量、静态变量、通过参数传入的对象，这些都安全。函数体内的局部变量，绝对不安全。编译器通常会对此发出警告，但不是所有情况都能检测到——所以这条规则必须刻在脑子里。

## 第五步——const 引用与临时对象

C++ 中有一个乍一看很奇怪的特性：`const` 引用可以绑定到临时对象（右值）上，并且会**延长这个临时对象的生命周期**，让它和引用共存亡。

```cpp
const int& ref = 42;  // OK！42 本来是个临时值
// ref 在整个作用域内有效，值为 42
```

这行代码做了什么？字面量 `42` 本来是一个右值，按理说在表达式结束之后就该消失。但因为 `ref` 是一个 `const` 引用并且直接绑定到了这个临时值，C++ 规定编译器必须把这个临时值的生命周期延长到 `ref` 的作用域结束。也就是说，编译器在幕后悄悄创建了一个临时 `int`，用 42 初始化它，然后让 `ref` 绑定到这个临时 `int` 上。

对 `int` 来说这没什么大不了的，但对复杂类型就很关键了：

```cpp
std::string get_name();

const std::string& name = get_name();
// get_name() 返回的临时 string 本来在完整表达式结束后就该销毁
// 但 const 引用绑定了它，生命周期被延长到 name 的作用域结束
// 所以 name 在整个作用域内都是安全的
```

不过这里有一个重要的限定条件——**引用必须直接绑定到临时对象**，生命延长才会生效。如果中间经过了函数返回等间接环节，规则就不成立了。这个话题涉及到返回值优化和移动语义，后面相关章节会展开讨论。

你可能注意到，非 const 引用不能绑定到临时对象：`int& ref = 42;` 编译不过去。原因也很合理——如果允许非 const 引用绑定临时值，那通过引用修改的就是一个马上要消失的对象，修改毫无意义。`const` 引用之所以可以，是因为它承诺了只读，编译器知道你不会去改那个临时值，所以安心地帮你延长寿命就好。

## 实战演练——references.cpp

我们把前面学到的内容整合到一个完整的程序里，重点对比引用和指针的使用差异：

```cpp
// references.cpp
// Platform: host
// Standard: C++17

#include <iostream>
#include <string>

struct SensorData {
    float temperature;
    float humidity;
    float pressure;
};

/// @brief 通过引用交换两个变量的值
void swap_by_ref(int& a, int& b)
{
    int temp = a;
    a = b;
    b = temp;
}

/// @brief 通过 const 引用打印 SensorData（不拷贝，不修改）
void print_sensor(const SensorData& data)
{
    std::cout << "温度: " << data.temperature << "°C, "
              << "湿度: " << data.humidity << "%, "
              << "气压: " << data.pressure << " hPa"
              << std::endl;
}

/// @brief 返回成员引用，允许外部修改
class Sensor {
    SensorData data_;

public:
    Sensor(float t, float h, float p)
        : data_{t, h, p}
    {
    }

    float& temperature() { return data_.temperature; }
    const SensorData& reading() const { return data_; }
};

int main()
{
    // --- 交换变量 ---
    int x = 10, y = 20;
    std::cout << "交换前: x=" << x << ", y=" << y << std::endl;
    swap_by_ref(x, y);
    std::cout << "交换后: x=" << x << ", y=" << y << std::endl;

    // --- const 引用传递大对象 ---
    SensorData reading{25.5f, 60.0f, 1013.25f};
    std::cout << "\n传感器读数: ";
    print_sensor(reading);

    // --- 返回成员引用 ---
    Sensor s(22.0f, 55.0f, 1000.0f);
    std::cout << "\n修改前: ";
    print_sensor(s.reading());

    s.temperature() = 30.0f;
    std::cout << "修改后: ";
    print_sensor(s.reading());

    // --- const 引用绑定临时对象 ---
    const std::string& label = std::string("温度传感器 #1");
    std::cout << "\n标签: " << label << std::endl;

    return 0;
}
```

编译运行：

```bash
g++ -std=c++17 -Wall -Wextra -o references references.cpp
./references
```

运行结果：

```text
交换前: x=10, y=20
交换后: x=20, y=10

传感器读数: 温度: 25.5°C, 湿度: 60%, 气压: 1013.25 hPa

修改前: 温度: 22°C, 湿度: 55%, 气压: 1000 hPa
修改后: 温度: 30°C, 湿度: 55%, 气压: 1000 hPa

标签: 温度传感器 #1
```

我们逐段来回顾一下这个程序做了什么。`swap_by_ref` 用引用参数实现了变量交换，调用时直接传变量名，不需要取地址符。`print_sensor` 用 `const SensorData&` 接收参数，既避免了结构体的拷贝开销，又在类型系统层面保证了函数不会修改传入的数据——调用者一看函数签名就放心了。`Sensor::temperature()` 返回成员变量的引用，外部代码拿到引用后可以直接赋值，实现了对内部数据的受控访问。最后的 `const std::string& label` 展示了 const 引用延长临时对象生命周期的能力——`std::string("温度传感器 #1")` 本来是个马上要消失的临时对象，但因为被 const 引用绑定了，它一直活到了 `main` 函数结束。

## 动手试试

### 练习一：改造指针函数

下面这个函数用指针实现了一个简单的"将数组元素翻倍"的功能。把它改成引用版本：

```cpp
void double_values(int* arr, int n)
{
    for (int i = 0; i < n; ++i) {
        arr[i] *= 2;
    }
}
```

提示：C 风格数组没法直接用引用传参来保留长度信息，可以考虑用 `std::array<int, N>` 替代。

### 练习二：找错

下面这段代码有几处与引用相关的问题，把它们全部找出来：

```cpp
int& get_value()
{
    int x = 42;
    return x;
}

void process(int& ref) { ref += 10; }

int main()
{
    int& r = get_value();
    int& uninit;              // 行 A
    int a = 10;
    int& ref = a;
    int b = 20;
    ref = &b;                 // 行 B
    process(5);               // 行 C
}
```

逐行分析：哪几行有编译错误？哪几行是运行时的未定义行为？

### 练习三：实现一个简单的链式配置器

设计一个类 `Config`，包含 `width_` 和 `height_` 两个 `int` 成员。提供 `set_width(int)` 和 `set_height(int)` 两个方法，让它们返回 `Config&` 以支持链式调用：

```cpp
Config c;
c.set_width(800).set_height(600);
```

## 小结

这一章我们从"指针的痛点"出发，学习了 C++ 引用这个核心特性。引用是现有对象的别名，声明时必须初始化，绑定后不可更改。和指针相比，引用没有空值、不需要解引用语法、绑定关系不可变——这些约束恰恰让它成为"传递确定存在的对象"时的最佳选择。

作为函数参数时，引用让代码比指针版本更干净；加上 `const` 修饰后，它变成了"不拷贝、不修改"的只读传参标准范式。返回引用时要格外小心，必须确保被引用对象的生命周期长于函数调用——局部变量绝对不能返回引用。最后，`const` 引用可以绑定临时对象并延长其生命周期，这个特性在实际代码中很常见，但也仅限于 const 引用。

下一章我们会接触到 C++ 动态内存管理的基础——虽然现在还不到讲智能指针的时候，但你可以先有个印象：现代 C++ 通过 RAII 和智能指针把"谁负责释放内存"这个问题彻底解决了。在那之前，先保证引用的基础扎实，后面会轻松很多。
