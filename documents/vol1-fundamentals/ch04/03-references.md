---
chapter: 4
cpp_standard:
- 11
- 14
- 17
- 20
description: 收拢前三章用过的引用规则，与指针逐项对比给出选择判据，补上返回引用的链式用法与 const 引用延长临时对象生命周期的边界条件。
difficulty: beginner
order: 3
platform: host
prerequisites:
- 指针运算与数组
reading_time_minutes: 14
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
title: 引用
---
# 引用：给变量起个别名，少受点指针的罪

指针的两篇走完，解引用、取地址、指针运算咱们都上手了。不过回过头看引用之前，有件事值得点破：您其实早就用过它。第 1 章讲值类别，咱们靠引用的绑定规则判断什么能接、什么不能接；第 2 章的 range-for，`auto&` 拿到的就是原元素的引用；第 3 章的参数传递，`swap` 和 `const std::string&` 更是场场主角。引用一直在场，只是一直没和指针面对面比过。

这篇就做这件事：把散在三章里的规则收拢一遍，再让引用和指针逐项对比。看完您手里会有一个明确的选择判据，什么场景用引用，什么场景非指针不可。

## 收拢：引用的三条规则

引用是一个已经存在的变量的**别名**。`int& ref = value;` 之后，`ref` 和 `value` 指的是同一个对象，对 `ref` 的任何操作都作用在 `value` 上。底层实现上引用通常借道指针，但语言层面把指针那些危险操作都收走了，留下的就是一个干净的"另一个名字"。这套说法您在第 1 章值类别那篇见过原型，这里正式定型。

三条规则，前面各章咱们都踩过点，现在放在一起说。引用**必须在声明时初始化**，`int& ref;` 编译不过，它没有"先空着、回头再绑"的选项，这一点和指针的 `nullptr` 形成第一个对照。引用**一旦绑定就不能换目标**，C++ 根本没有"重新绑定引用"的语法。还有一条：严格来说**不存在空引用**，语言要求引用绑定到一个有效对象。这三条既是引用安全性的来源，也是它能力边界的来源，后面和指针对比时会反复用到。

"不能换目标"这一点咱们特别容易踩坑，单独拎出来看：

```cpp
int value = 42;
int& ref = value;

int other = 200;
ref = other;  // 这不是"让 ref 指向 other"！
```

`ref = other;` 的实际效果，是把 `other` 的值 200 赋给 `ref` 所引用的对象，也就是 `value`。执行完，`value` 变成 200，`ref` 依然是 `value` 的引用，和 `other` 再无关系。引用的绑定是一次性的，之后所有对 `ref` 的赋值，都只是在修改被引用对象的值。您要是需要"改指向"的语义，该上场的工具就是指针。

## 引用与指针，到底选谁

既然两者都能实现"间接操作对象"，咱们把区别逐条摆开。

引用声明时必须绑定到一个对象，所以一个引用从诞生起就是"有效的"（前提是您没搞出悬空引用这种高级 bug）；指针可以先设成 `nullptr` 再说，灵活，代价是每次使用前都得掂量它会不会是空的。绑定之后能不能换目标？引用一旦绑定终身不变；指针随时可以指向别的对象，要"迭代器式"地遍历内存、要表达"现在没有对象"的语义，只能靠指针。语法负担也不同：引用用起来和普通变量一样，直接写名字；指针要用 `*ptr` 或 `ptr->member`，代码明显啰嗦。再加上不存在空引用这条，指针可以为 `nullptr` 既是它的灵活，也是大量 bug 的来源。

还有一个实际限制您可能会忽略：引用存不进容器。`std::vector<int&>` 这样的写法编译不过，标准容器要求元素是对象，而引用只是别名，不是对象。想把"一批可变的指向"攒在一起管理，还是指针（或者以后会遇到的 `std::reference_wrapper`）。

两边差异在函数调用上看得最清楚。咱们拿"交换两个变量"这个老任务，分别用指针和引用写一遍。C 风格只能传指针：

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

用引用重写，世界清净了：

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

函数体内没有 `*` 解引用，调用处不用 `&` 取地址。标准库的 `std::swap` 也是用引用实现的，原理和 `swap_by_reference` 一模一样。第 3 章咱们写它的时候，引用还是"先记住用法的工具"，现在可以补上完整的解释了：`a`、`b` 就是调用者那两个变量的别名。

> PS: 使用引用还是指针，说到底看您，我一般表达之后就是使用这个对象的时候，使用引用，如果表达存储对象的位置更多（去那找！），采用的是指针。语义区分，不能说欸引用不会悬空，完全错误的说法。

## 返回引用：链式调用，悬垂警告说精确

引用作为函数参数您已经写熟了，返回值这边还有两个模式值得看。头一个是返回类成员的引用，让外部代码直接读写内部数据：

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

另一个是**链式调用**：让成员函数返回 `*this` 的引用，调用者就能在一行代码里串联多个操作。`std::cout << a << b << c;` 能连续输出，靠的就是每次 `<<` 都返回 `std::cout` 的引用，这个机制咱们天天在用。

至于返回局部变量的引用，第 1 章和第 3 章各警告过一次，案例咱们也亲手修过，这里不重讲，只留一条精确的判断规则：

::: warning 返回引用的安全判断
被引用对象的生命周期必须长于函数调用本身。成员变量、全局变量、静态变量、通过参数传入的对象，都安全；函数体内定义的局部变量，绝对不安全。编译器对"直接返回局部变量"这类简单形态通常会给警告，但覆盖不了所有路径，这条规则得记在咱们自己脑子里。
:::

## const 引用与临时对象：延长只认直接绑定

const 引用能绑定到临时对象并**延长它的生命周期**，这条规则第 1 章讲值类别时咱们就用过：`const int& ref = 42;` 合法，`ref` 在整个作用域内有效。函数按值返回、外面用 const 引用直接接住返回值，同样算直接绑定：

```cpp
std::string get_name();

const std::string& name = get_name();
// 按值返回的临时 string 被 name 直接接住
// 生命周期延长到 name 的作用域结束，安全
```

但"直接"两个字是关键，转一手就没人管了。咱们看一个能编译、能跑、但已经悬垂的例子：

```cpp
const std::string& pick(const std::string& a, const std::string& b)
{
    return a.size() > b.size() ? a : b;  // 把引用参数原样返回
}

const std::string& best = pick("hello", "hi");  // 悬垂！
```

`"hello"` 和 `"hi"` 作为实参传给 `pick` 的引用参数时，这两个临时对象的生命周期只到整条表达式结束；`pick` 返回的是其中一个的引用，`best` 接住的时候它们已经销毁了。这段代码咱们真跑过：GCC 16.2 加 `-Wall` 会给一条 `-Wdangling-reference` 警告，Clang 22.1 加 `-Wall -Wextra` 一声不吭；而且就算带着警告，程序照样编译、照样输出 `hello`。结果碰巧对，不代表行为有定义，悬垂引用的可怕之处就在这，它不崩给您看。

非 const 引用不能绑定到临时对象（`int& ref = 42;` 编译不过），原因咱们在第 3 章说过：真允许的话，通过引用改的就是一个马上要消失的对象，修改毫无意义。`-Wdangling-reference` 这类检查是启发式的，形态稍一变化就可能漏掉，所以规则本身得记牢；它背后牵扯的返回值优化和移动语义，留到后面的章节展开。

## 实战演练——references.cpp

咱们把这篇收拢的内容放进一个完整程序，重点看引用和指针在使用形态上的差异：

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

咱们逐段回顾这个程序做了什么。`swap_by_ref` 用引用参数实现变量交换，调用时直接传变量名，不需要取地址符。`print_sensor` 用 `const SensorData&` 接收参数，既避免了结构体的拷贝开销，又在类型系统层面保证了函数不会修改传入的数据，调用者一看函数签名就放心。`Sensor::temperature()` 返回成员变量的引用，外部代码拿到引用后可以直接赋值，实现了对内部数据的受控访问。最后的 `const std::string& label` 展示了直接绑定下临时对象生命周期的延长：`std::string("温度传感器 #1")` 本来是个马上要消失的临时对象，被 const 引用接住后，一直活到了 `main` 函数结束。

## 动手试试

### 练习一：改造指针函数

下面这个函数用指针实现了一个简单的"将数组元素翻倍"的功能。请您把它改成引用版本：

```cpp
void double_values(int* arr, int n)
{
    for (int i = 0; i < n; ++i) {
        arr[i] *= 2;
    }
}
```

提示：C 风格数组按引用传参要写成 `int (&arr)[5]` 这种"数组的引用"，长度是类型的一部分，函数没法通用于任意长度；咱们更省事的替代是 `std::array<int, N>`。

::: details 参考答案

```cpp
#include <iostream>
#include <array>

void double_values(std::array<int, 5>& arr)
{
    for (auto& value : arr) {
        value *= 2;
    }
}

int main()
{
    std::array<int, 5> values{1, 2, 3, 4, 5};

    std::cout << "修改前: ";
    for (const auto& value : values) {
        std::cout << value << " ";
    }
    std::cout << std::endl;
    double_values(values);
    std::cout << "修改后: ";
    for (const auto& value : values) {
        std::cout << value << " ";
    }
    std::cout << std::endl;
    return 0;
}
```

编译运行:

```bash
g++ -std=c++17 -Wall -Wextra main.cpp -o main && ./main
```

运行结果:

```text
修改前: 1 2 3 4 5
修改后: 2 4 6 8 10
```

:::

### 练习二：找错

下面这段代码有几处与引用相关的问题，请您把它们全部找出来：

```cpp
int& get_value()
{
    int x = 42;
    return x;
}

void process(int& ref) { ref += 10; }

int main()
{
    int& r = get_value(); // 行 A
    int& uninit;          // 行 B
    int a = 10;
    int& ref = a;
    int b = 20;
    ref = &b;             // 行 C
    process(5);           // 行 D
}
```

请您逐行分析：哪几行有编译错误？哪几行是运行时的未定义行为？

::: details 参考答案

咱们先给结论：**行 B、行 C、行 D 是编译错误；按题目的二分法，运行时隐患记为行 A。** 更精确地说，`get_value()` 的返回语句和行 A 可以编译，但会留下悬空引用；对 `r` 的读写才会触发运行时未定义行为。

| 位置 | 结果 | 原因 |
| --- | --- | --- |
| `return x;` | 可编译，编译器通常会警告 | `x` 是自动存储期的局部变量，函数返回时它的生命周期结束；返回的 `int&` 不会把 `x` 的生命周期延长。 |
| 行 A：`int &r = get_value();` | 可编译，但 `r` 是悬空引用 | `r` 绑定到已经结束生命周期的 `x`。这一步本身只是制造了悬空引用；后续通过 `r` 读取或写入（例如 `std::cout << r`）才是未定义行为。 |
| 行 B：`int &uninit;` | **编译错误** | 引用声明必须在声明时初始化，不能像指针那样先声明、之后再绑定。 |
| `int a = 10;`、`int &ref = a;`、`int b = 20;` | 正确 | `ref` 在声明时绑定到了仍然存活的 `a`。 |
| 行 C：`ref = &b;` | **编译错误** | `ref` 表达式的类型是 `int`，而 `&b` 的类型是 `int*`；并且赋值不会让引用改绑到另一个对象。给 `a` 赋 `b` 的值应写 `ref = b`，若要改指向则必须重新声明引用或改用指针。 |
| 行 D：`process(5);` | **编译错误** | `process` 要求可修改的 `int&`，而字面量 `5` 是右值，不能绑定到非 `const` 左值引用。应传入一个具名的 `int` 左值。 |

咱们最容易混淆的是行 A：严格地说，**悬空引用是错误状态，访问悬空引用才是运行时未定义行为**。例如把 B、C、D 暂时注释掉后，下面的读取就会触发 UB：

```cpp
int &r = get_value();
std::cout << r;  // 未定义行为：r 指向的 x 已经结束生命周期
```

咱们给出一种安全的修正版：让 `get_value` 按值返回，并在声明时为引用提供初始对象；如果确实要返回引用，则只能返回生命周期足够长的对象（例如静态对象或调用者传入的对象）：

```cpp
int get_value()
{
    return 42;
}

void process(int& ref) { ref += 10; }

int main()
{
    int r = get_value();

    int data = 0;
    int& uninit = data;

    int a = 10;
    int& ref = a;
    int b = 20;
    ref = b;

    int value = 5;
    process(value);

    (void)r;
    (void)uninit;
}
```

:::

### 练习三：实现一个简单的链式配置器

请您设计一个类 `Config`，包含 `width_` 和 `height_` 两个 `int` 成员，提供 `set_width(int)` 和 `set_height(int)` 两个方法，让它们返回 `Config&` 以支持链式调用：

```cpp
Config c;
c.set_width(800).set_height(600);
```

::: details 参考答案

```cpp
#include <iostream>

class Config {
    int width_{};
    int height_{};

public:
    Config& set_width(int width)
    {
        width_ = width;
        return *this;
    }
    Config& set_height(int height)
    {
        height_ = height;
        return *this;
    }
};
int main()
{
    Config c;
    c.set_width(800).set_height(600);
    return 0;
}
```

:::
