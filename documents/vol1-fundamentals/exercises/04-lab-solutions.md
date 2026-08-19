---
title: "卷 1 · 基础 Lab 实验参考"
description: "卷 1 Lab（气象站——从一条读数到一台解码器）的实验参考：六个步骤加 L5 挑战的逐步解答与完整代码，每步标注知识点链接，所有输出在 WSL Arch（g++ 16.1.1）真实运行得到。"
chapter: 1
order: 4
tags: [host, beginner, cpp-modern, 实战]
difficulty: beginner
platform: host
cpp_standard: [11, 14, 17]
reading_time_minutes: 35
prerequisites: []
related: []
---

# 卷 1 · 基础 Lab 实验参考

> 所有输出在 WSL Arch（g++ 16.1.1）下真实运行得到。建议卡住时先看「思路」逐步对照，不要直接抄完整代码——参考实现只是**一种**过关方式，验收标准对得上就都是对的。

## 步骤 1：类型侦察 {#lab-1}

**思路**：`sizeof` + `numeric_limits` + `<cstdint>` 三件套一次摸清本机；`long == 8` 说明本机是 LP64——但这是**实现定义**的事实，不能写进代码假设。

1. 打印整型家族与 `double` 的大小、`int`/`unsigned` 的取值范围、定宽类型的大小。→ 知识点：[基本数据类型](../ch01/01-basic-types.md)「整数家族」「类型的极限」「固定宽度类型」三节
2. 本机 `long = 8` 是 LP64 模型；64 位 Windows 的 LLP64 下 `long` 是 4 字节——所以协议、存档、二进制布局必须用 `int32_t` 这类定宽类型，永远别赌 `long`。→ 知识点：[基本数据类型](../ch01/01-basic-types.md)「固定宽度类型——跨平台的定心丸」一节

```cpp
#include <cstdint>
#include <iostream>
#include <limits>

int main()
{
    std::cout << "char:        " << sizeof(char) << " 字节" << std::endl;
    std::cout << "short:       " << sizeof(short) << " 字节" << std::endl;
    std::cout << "int:         " << sizeof(int) << " 字节" << std::endl;
    std::cout << "long:        " << sizeof(long) << " 字节" << std::endl;
    std::cout << "long long:   " << sizeof(long long) << " 字节" << std::endl;
    std::cout << "double:      " << sizeof(double) << " 字节" << std::endl;

    std::cout << "int 范围:    " << std::numeric_limits<int>::min()
              << " ~ " << std::numeric_limits<int>::max() << std::endl;
    std::cout << "unsigned 范围: 0 ~ " << std::numeric_limits<unsigned int>::max() << std::endl;

    std::cout << "int32_t:     " << sizeof(int32_t) << " 字节" << std::endl;
    std::cout << "int64_t:     " << sizeof(int64_t) << " 字节" << std::endl;
    std::cout << "uint8_t:     " << sizeof(uint8_t) << " 字节" << std::endl;
    std::cout << "size_t:      " << sizeof(size_t) << " 字节" << std::endl;
    return 0;
}
```

**验证输出**：

```text
$ g++ -std=c++17 -Wall -Wextra lab1.cpp -o lab1 && ./lab1
char:        1 字节
short:       2 字节
int:         4 字节
long:        8 字节
long long:   8 字节
double:      8 字节
int 范围:    -2147483648 ~ 2147483647
unsigned 范围: 0 ~ 4294967295
int32_t:     4 字节
int64_t:     8 字节
uint8_t:     1 字节
size_t:      8 字节
```

## 步骤 2：编译期温度换算 {#lab-2}

**思路**：`constexpr` 函数的参数全在编译期已知时，求值发生在编译期——`static_assert` 既是验收，也是倒逼编译器真的在编译期算完。

1. 两个换算函数都标 `constexpr`；`static_assert` 用的三个值（0→32、-40→-40、32→0）在二进制浮点里都能**精确表示**，所以敢直接 `==` 比较。→ 知识点：[inline 与 constexpr 函数](../ch03/04-inline-constexpr.md)「编译期计算的实际案例」一节
2. 运行时读入打印、对照表、往返换算的容差比较——浮点「不精确」的根源是有限尾数装不下无限循环小数，所以比较永远走容差。→ 知识点：[类型转换](../ch01/02-type-conversion.md)「浮点数比较的不可靠性」一节

```cpp
#include <cmath>
#include <iostream>

constexpr double c_to_f(double c)
{
    return c * 9.0 / 5.0 + 32.0;
}

constexpr double f_to_c(double f)
{
    return (f - 32.0) * 5.0 / 9.0;
}

static_assert(c_to_f(0.0) == 32.0, "0C 应为 32F");
static_assert(c_to_f(-40.0) == -40.0, "-40C 应为 -40F");
static_assert(f_to_c(32.0) == 0.0, "32F 应为 0C");

int main()
{
    double c = 0.0;
    std::cout << "请输入摄氏温度: ";
    std::cin >> c;
    std::cout << c << " C = " << c_to_f(c) << " F" << std::endl;

    std::cout << "--- 对照表 ---" << std::endl;
    double marks[] = {-40.0, 0.0, 25.0, 37.0};
    for (int i = 0; i < 4; ++i) {
        std::cout << marks[i] << " C = " << c_to_f(marks[i]) << " F" << std::endl;
    }

    const double kEpsilon = 1e-9;
    std::cout << "f_to_c(c_to_f(25.0)) 回到 25.0? "
              << (std::fabs(f_to_c(c_to_f(25.0)) - 25.0) < kEpsilon ? "是(容差内)" : "否")
              << std::endl;
    return 0;
}
```

**验证输出**：

```text
$ g++ -std=c++17 -Wall -Wextra lab2.cpp -o lab2 && echo "25" | ./lab2
请输入摄氏温度: 25 C = 77 F
--- 对照表 ---
-40 C = -40 F
0 C = 32 F
25 C = 77 F
37 C = 98.6 F
f_to_c(c_to_f(25.0)) 回到 25.0? 是(容差内)
```

-40 C = -40 F 是摄氏和华氏两条刻度唯一的交点，拿它当测试点非常合适。

## 步骤 3：温度序列清洗 {#lab-3}

**思路**：异常值的个数编译期不知道——`std::array` 大小是定死的，所以清洗结果接进 `std::vector`；`minmax_element` 一趟拿两个极值。

1. range-for 过滤：合法读数进 vector，异常值打印一行。-99.0 和 88.8 都落在合理范围 $[-50, 60]$ 之外。→ 知识点：[range-for 循环](../ch02/03-range-for.md)「第二步——搭配 auto 的三种姿势」一节
2. `std::minmax_element` + range-for 累加——统计全部用现成接口，不手写比较循环。→ 知识点：[std::array](../ch05/02-std-array.md)「实战：用 std::array 重写 C 数组代码」一节
3. 固定大小数组 vs 动态容器：`std::array` 零开销但大小编译期定死；`vector` 能随异常值数量伸缩。→ 知识点：[std::vector 快速上手](../ch11/01-vector.md)「从零开始——构造一个 vector」一节

```cpp
#include <algorithm>
#include <array>
#include <iostream>
#include <vector>

int main()
{
    std::array<double, 8> raw = {22.5, 19.8, -99.0, 25.1, 88.8, 21.3, 24.0, 20.2};

    // 合理范围 [-50, 60],之外的读数算异常值
    std::vector<double> cleaned;
    for (const auto& t : raw) {
        if (t >= -50.0 && t <= 60.0) {
            cleaned.push_back(t);
        } else {
            std::cout << "剔除异常值: " << t << std::endl;
        }
    }

    auto min_max_pair = std::minmax_element(cleaned.begin(), cleaned.end());
    double total = 0.0;
    for (const auto& t : cleaned) {
        total += t;
    }

    std::cout << "清洗后 " << cleaned.size() << " 个读数: ";
    for (const auto& t : cleaned) {
        std::cout << t << " ";
    }
    std::cout << std::endl;
    std::cout << "最低 " << *min_max_pair.first
              << ", 最高 " << *min_max_pair.second
              << ", 平均 " << total / static_cast<double>(cleaned.size()) << std::endl;
    return 0;
}
```

**验证输出**：

```text
$ g++ -std=c++17 -Wall -Wextra lab3.cpp -o lab3 && ./lab3
剔除异常值: -99
剔除异常值: 88.8
清洗后 6 个读数: 22.5 19.8 25.1 21.3 24 20.2
最低 19.8, 最高 25.1, 平均 22.15
```

$\frac{132.9}{6} = 22.15$ ✓。

## 步骤 4：Temperature 类 {#lab-4}

**思路**：「温度不低于绝对零度」这条不变量从构造函数开始守，每个修改入口再守一次——`operator+=` 里 `clamp()` 是因为两个合法温度的差可能越过下界。

1. 构造函数钳制一次，保证「出生即合法」；`+=`/`-=` 修改后再钳一次，保证「每次运算后仍合法」。→ 知识点：[类的定义](../ch06/01-class-basics.md)「访问控制：public、private、protected」一节
2. 复合赋值做成员返回 `*this` 引用，二元运算做非成员按值取左操作数复用——照教材 `Fraction` 的模式。→ 知识点：[算术与比较运算符](../ch07/01-arithmetic-comparison.md)「从 `operator+=` 开始搭建算术运算」一节
3. `==`/`!=`/`<` 全部锚定 `as_celsius()` 这一个真相源。→ 知识点：[算术与比较运算符](../ch07/01-arithmetic-comparison.md)「比较运算符」一节

```cpp
#include <iostream>

class Temperature {
private:
    double celsius_;

    void clamp()
    {
        if (celsius_ < -273.15) {
            celsius_ = -273.15;   // 绝对零度钳制
        }
    }

public:
    explicit Temperature(double c = 0.0) : celsius_(c) { clamp(); }

    double as_celsius() const { return celsius_; }
    double as_fahrenheit() const { return celsius_ * 9.0 / 5.0 + 32.0; }
    double as_kelvin() const { return celsius_ + 273.15; }

    Temperature& operator+=(const Temperature& rhs)
    {
        celsius_ += rhs.celsius_;
        clamp();
        return *this;
    }

    Temperature& operator-=(const Temperature& rhs)
    {
        celsius_ -= rhs.celsius_;
        clamp();
        return *this;
    }
};

Temperature operator+(Temperature lhs, const Temperature& rhs)
{
    lhs += rhs;
    return lhs;
}

Temperature operator-(Temperature lhs, const Temperature& rhs)
{
    lhs -= rhs;
    return lhs;
}

bool operator==(const Temperature& l, const Temperature& r)
{
    return l.as_celsius() == r.as_celsius();
}

bool operator!=(const Temperature& l, const Temperature& r)
{
    return !(l == r);
}

bool operator<(const Temperature& l, const Temperature& r)
{
    return l.as_celsius() < r.as_celsius();
}

std::ostream& operator<<(std::ostream& os, const Temperature& t)
{
    os << t.as_celsius() << "C";
    return os;
}

int main()
{
    Temperature a(25.0), b(-10.0);
    std::cout << "a = " << a << ", b = " << b << std::endl;
    std::cout << "a + b = " << (a + b) << std::endl;
    std::cout << "a - b = " << (a - b) << std::endl;
    std::cout << "a 的华氏: " << a.as_fahrenheit() << "F, 开尔文: " << a.as_kelvin() << "K" << std::endl;

    Temperature c(-280.0);
    std::cout << "c(-280 输入) = " << c << " (被钳制)" << std::endl;

    std::cout << "a < b ? " << (a < b ? "是" : "否") << std::endl;
    std::cout << "a != b ? " << (a != b ? "是" : "否") << std::endl;
    return 0;
}
```

**验证输出**：

```text
$ g++ -std=c++17 -Wall -Wextra lab4.cpp -o lab4 && ./lab4
a = 25C, b = -10C
a + b = 15C
a - b = 35C
a 的华氏: 77F, 开尔文: 298.15K
c(-280 输入) = -273.15C (被钳制)
a < b ? 否
a != b ? 是
```

## 步骤 5：多态传感器阵列 {#lab-5}

**思路**：`ISensor` 是「能力契约」，两个具体传感器是「签约方」；容器装指针不装值，是因为抽象类不可实例化、按值又会切片——`unique_ptr` 负责每个对象的生命周期。

1. 抽象基类 = 纯虚函数 + 虚析构；派生类必须把纯虚函数全部 override 才能实例化。→ 知识点：[抽象类与接口](../ch08/03-abstract-classes.md)「纯虚函数与抽象类的诞生」一节
2. `std::vector<std::unique_ptr<ISensor>>` 统一管理不同实现，遍历时虚函数按实际类型分发。→ 知识点：[虚函数与多态](../ch08/02-virtual-functions.md)「vtable 揭秘」一节、[智能指针预告](../ch04/04-smart-ptr-preview.md)「unique_ptr——独占所有权」一节
3. 加风速计只要新写一个派生类再 `make_unique` 进容器——主循环零改动，这就是解耦的收益。→ 知识点：[抽象类与接口](../ch08/03-abstract-classes.md)「抽象类的设计思路」一节

```cpp
#include <iostream>
#include <memory>
#include <string>
#include <vector>

class ISensor {
public:
    virtual ~ISensor() = default;
    virtual std::string name() const = 0;
    virtual double read() const = 0;
};

class TemperatureSensor : public ISensor {
private:
    std::string id_;
    double value_;

public:
    TemperatureSensor(const std::string& id, double value) : id_(id), value_(value) {}
    std::string name() const override { return "温度计 " + id_; }
    double read() const override { return value_; }
};

class HumiditySensor : public ISensor {
private:
    std::string id_;
    double value_;

public:
    HumiditySensor(const std::string& id, double value) : id_(id), value_(value) {}
    std::string name() const override { return "湿度计 " + id_; }
    double read() const override { return value_; }
};

int main()
{
    std::vector<std::unique_ptr<ISensor>> station;
    station.push_back(std::make_unique<TemperatureSensor>("T-01", 23.5));
    station.push_back(std::make_unique<HumiditySensor>("H-01", 61.0));
    station.push_back(std::make_unique<TemperatureSensor>("T-02", 24.1));

    double total = 0.0;
    for (const auto& s : station) {
        std::cout << s->name() << " -> " << s->read() << std::endl;
        total += s->read();
    }
    std::cout << "读数平均值: " << total / static_cast<double>(station.size()) << std::endl;
    return 0;
}
```

**验证输出**：

```text
$ g++ -std=c++17 -Wall -Wextra lab5.cpp -o lab5 && ./lab5
温度计 T-01 -> 23.5
湿度计 H-01 -> 61
温度计 T-02 -> 24.1
读数平均值: 36.2
```

$\frac{23.5 + 61.0 + 24.1}{3} = 36.2$ ✓。

## 步骤 6：异常与 RAII {#lab-6}

**思路**：解析失败就抛异常，调用端分层兜住；`ScopedReport` 声明在 try 块内部——解析抛异常时，栈展开先析构 guard（打印「报告已生成」），控制流才落到 catch 块（打印「参数错误」），所以输出的先后顺序本身就是栈展开的实证。

1. `parse_reading` 的三类非法输入统一转成 `std::invalid_argument`——`stod` 的异常在内部 catch 住后转译，调用端只需要关心一种类型。→ 知识点：[异常基础](../ch10/01-try-catch.md)「点火——throw、try、catch 三件套」一节
2. 守卫类的析构在正常路径和异常路径都会被调用——这就是 RAII「不漏成为默认行为」。→ 知识点：[异常安全](../ch10/02-exception-safety.md)「RAII 与异常安全」一节
3. catch 从具体到一般排列，`invalid_argument` 在前、`exception` 兜底。→ 知识点：[异常基础](../ch10/01-try-catch.md)「标准异常层次」一节

```cpp
#include <iostream>
#include <stdexcept>
#include <string>

// RAII 守卫:析构时必定打印一行,正常路径和异常路径都逃不掉
class ScopedReport {
private:
    const char* label_;

public:
    explicit ScopedReport(const char* label) : label_(label)
    {
        std::cout << "[" << label_ << "] 开始采集" << std::endl;
    }

    ~ScopedReport()
    {
        std::cout << "[" << label_ << "] 报告已生成(析构自动执行)" << std::endl;
    }

    ScopedReport(const ScopedReport&) = delete;
    ScopedReport& operator=(const ScopedReport&) = delete;
};

struct Reading {
    std::string sensor;
    double value;
};

// 解析一行 "TEMP,23.5":非法输入抛异常
Reading parse_reading(const std::string& line)
{
    std::size_t comma = line.find(',');
    if (comma == std::string::npos) {
        throw std::invalid_argument("缺少逗号分隔符: " + line);
    }
    std::string sensor = line.substr(0, comma);
    if (sensor.empty()) {
        throw std::invalid_argument("传感器名不能为空: " + line);
    }
    double value = 0.0;
    try {
        value = std::stod(line.substr(comma + 1));
    } catch (const std::exception&) {
        throw std::invalid_argument("数值非法: " + line.substr(comma + 1));
    }
    return Reading{sensor, value};
}

int main()
{
    const char* samples[] = {"TEMP,23.5", "HUMI,61.0", "TEMP,abc", "PRES,1013.2", "BROKEN"};
    for (const char* s : samples) {
        try {
            ScopedReport guard("轮次");
            Reading r = parse_reading(s);
            std::cout << "  解析成功: " << r.sensor << " = " << r.value << std::endl;
        } catch (const std::invalid_argument& e) {
            std::cout << "  参数错误: " << e.what() << std::endl;
        } catch (const std::exception& e) {
            std::cout << "  其他错误: " << e.what() << std::endl;
        }
    }
    return 0;
}
```

**验证输出**（普通构建与 ASan 构建各一份，均零报告）：

```text
$ g++ -std=c++17 -Wall -Wextra lab6.cpp -o lab6 && ./lab6
[轮次] 开始采集
  解析成功: TEMP = 23.5
[轮次] 报告已生成(析构自动执行)
[轮次] 开始采集
  解析成功: HUMI = 61
[轮次] 报告已生成(析构自动执行)
[轮次] 开始采集
[轮次] 报告已生成(析构自动执行)
  参数错误: 数值非法: abc
[轮次] 开始采集
  解析成功: PRES = 1013.2
[轮次] 报告已生成(析构自动执行)
[轮次] 开始采集
[轮次] 报告已生成(析构自动执行)
  参数错误: 缺少逗号分隔符: BROKEN

$ g++ -std=c++17 -g -O0 -fsanitize=address,undefined lab6.cpp -o lab6_asan && ASAN_OPTIONS=detect_leaks=1 ./lab6_asan
[轮次] 开始采集
  解析成功: TEMP = 23.5
[轮次] 报告已生成(析构自动执行)
[轮次] 开始采集
  解析成功: HUMI = 61
[轮次] 报告已生成(析构自动执行)
[轮次] 开始采集
[轮次] 报告已生成(析构自动执行)
  参数错误: 数值非法: abc
[轮次] 开始采集
  解析成功: PRES = 1013.2
[轮次] 报告已生成(析构自动执行)
[轮次] 开始采集
[轮次] 报告已生成(析构自动执行)
  参数错误: 缺少逗号分隔符: BROKEN
退出码: 0
```

注意 `"TEMP,abc"` 和 `"BROKEN"` 两行：`[轮次] 报告已生成` 排在 `参数错误` **前面**——抛异常 → 栈展开 → guard 析构 → 才轮到 catch。如果 guard 声明在 try 外面，这个「析构先于 catch」的现象就不存在了，值得自己动手改一改对比一下。

## 附加挑战（L5）：徒手解码 float {#lab-l5}

**思路**：float 的 32 个比特 = 1 位符号 + 8 位指数（偏置 127）+ 23 位尾数（隐含前导 1）——IEEE 754 单精度结构是本 Lab 引入的教材外补充（教材 C 速成里只有 `3.14f` 的整体位模式 `0x4048F5C3`，没有这张拆位图）。C++ 里拿位模式必须走 `memcpy`（union 双关是 UB），$2^{e}$ 用循环乘除手搓。

1. `float_bits` 用 `memcpy` 把 4 字节按位拷进 `uint32_t`——这一行不会被优化成「一次真正的拷贝」，编译器会直接把它消化成寄存器间搬移，零开销。→ 知识点：[联合体、枚举、位域与 typedef](../c_tutorials/13-union-enum-bitfield-typedef.md)「用类型双关查看浮点数的二进制表示」一节（union 双关在 C 合法、在 C++ 是 UB，所以这里选 `memcpy`；符号|指数|尾数的拆位结构为本 Lab 教材外补充）、[类型转换](../ch01/02-type-conversion.md)「reinterpret_cast」一节（为什么这里不选它）
2. `power_of_two` 用循环乘 2 / 除 2 实现 $2^{e}$——乘 2 除 2 在二进制浮点里都是**精确**操作，所以解码结果和 `static_cast<double>(f)` 分毫不差（相对误差 0）。→ 知识点：[基本数据类型](../ch01/01-basic-types.md)「浮点数——精确与近似的博弈」一节
3. 规格化数 `raw_exp != 0`：`value = (1 + mant/2^23) * 2^(raw_exp-127)`，那个 `1` 就是 IEEE 754 的**隐含前导 1**；非规格化数（含 0）指数用 -126 且没有前导 1。→ 知识点：[基本数据类型](../ch01/01-basic-types.md)「浮点数——精确与近似的博弈」一节（规格化/非规格化的拆位规则为本 Lab 教材外补充）
4. 解码值和十进制 3.14 差 3.34e-08：float 只有 23 位尾数，「不准」发生在**把 3.14 存进 float 的那一刻**，解码只是忠实地重建了那个近似值。→ 知识点：[类型转换](../ch01/02-type-conversion.md)「浮点数比较的不可靠性」一节

```cpp
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>

// 提取 float 的位模式:C++ 里 union 类型双关是 UB,改用 memcpy
std::uint32_t float_bits(float f)
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &f, sizeof(bits));
    return bits;
}

// 纯算术实现 2^e:不用 pow/ldexp
double power_of_two(int e)
{
    double result = 1.0;
    if (e >= 0) {
        for (int i = 0; i < e; ++i) {
            result *= 2.0;
        }
    } else {
        for (int i = 0; i < -e; ++i) {
            result /= 2.0;
        }
    }
    return result;
}

// 按 IEEE 754 逐域解码 float(只处理规格化数、非规格化数与零)
double decode_float(float f)
{
    std::uint32_t bits = float_bits(f);
    std::uint32_t sign = (bits >> 31) & 1u;
    int raw_exp = static_cast<int>((bits >> 23) & 0xFFu);
    std::uint32_t mant = bits & 0x7FFFFFu;

    double value = 0.0;
    if (raw_exp == 0) {
        // 非规格化(含 0):指数用 -126,没有隐含前导 1
        value = static_cast<double>(mant) / 8388608.0;
        value *= power_of_two(-126);
    } else {
        // 规格化:value = (1 + mant / 2^23) * 2^(raw_exp - 127)
        value = 1.0 + static_cast<double>(mant) / 8388608.0;
        value *= power_of_two(raw_exp - 127);
    }
    return sign != 0 ? -value : value;
}

int main()
{
    float cases[] = {1.0f, 3.14f, -0.5f, 0.0f, 0.1f};
    for (float f : cases) {
        double decoded = decode_float(f);
        double ref = static_cast<double>(f);
        double err = (ref == 0.0) ? std::fabs(decoded)
                                  : std::fabs(decoded - ref) / std::fabs(ref);
        std::cout << "f=" << f << " 解码=" << decoded << " 相对误差=" << err << std::endl;
    }

    // 对照:float 与十进制 3.14 的差距在存储那一刻就决定了
    double gap = std::fabs(decode_float(3.14f) - 3.14) / 3.14;
    std::cout << "3.14f 解码值与十进制 3.14 的相对差: " << gap << std::endl;
    return 0;
}
```

**验证输出**：

```text
$ g++ -std=c++17 -Wall -Wextra labl5.cpp -o labl5 && ./labl5
f=1 解码=1 相对误差=0
f=3.14 解码=3.14 相对误差=0
f=-0.5 解码=-0.5 相对误差=0
f=0 解码=0 相对误差=0
f=0.1 解码=0.1 相对误差=0
3.14f 解码值与十进制 3.14 的相对差: 3.3409e-08
```

五个输入的解码值与 `double(f)` 完全一致（误差 0），与十进制 3.14 的差距是 3.3409e-08——解码器本身是精确的，float 的「不准」在 `3.14f` 这个字面量诞生时就已经注定了。

## 收尾

七步走完，气象站这条线把卷 1 的知识从「类型」一路串到了「字节」。最有价值的一步在最后：你亲手证明了 float 的不精确不是解码的错，而是 23 位尾数的物理极限——这种「追到字节层」的体验，就是后面几卷所有底层话题的入场券。接下来是[Project](./05-project.md)：把类、模板、STL 和异常盖成一座能住人的房子。
