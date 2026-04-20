---
title: "条件语句"
description: "掌握 if/else、switch 和三元运算符，学会用条件语句控制程序走向"
chapter: 2
order: 1
difficulty: beginner
reading_time_minutes: 12
platform: host
prerequisites:
  - "值类别简介"
tags:
  - cpp-modern
  - host
  - beginner
  - 入门
  - 基础
cpp_standard: [11, 14, 17, 20]
---

# 条件语句

如果程序永远只按照一条直线走到底，那它就和一个只会复读的机器没什么区别。现实中的程序需要做判断——"用户输入了负数？那就提示错误""传感器读数超过阈值？那就触发报警"。条件语句就是赋予程序这种"做决定"能力的机制。

这一章我们把 C++ 的条件语句从头到尾过一遍：`if/else`、`switch`、三元运算符，以及 C++17 带来的带初始化器的 `if`。表面上看着简单，但里面藏着不少容易踩的坑，特别是赋值和比较混淆、`switch` 穿透这类问题，在实际项目中都是高频 bug 来源。

## if 和 if-else——最基本的分支

`if` 语句的语法非常直白：括号里放一个条件表达式，如果条件为真（也就是能转换成 `true`），就执行后面的代码块。

```cpp
#include <iostream>

int main()
{
    int temperature = 38;

    if (temperature > 37) {
        std::cout << "温度偏高，请注意降温" << std::endl;
    }

    return 0;
}
```

运行结果：

```text
温度偏高，请注意降温
```

条件不满足的时候什么都不做，有时候不够用。我们需要一个"否则"的分支——这就是 `else`。更进一步，如果还有第三种、第四种情况，就可以用 `else if` 把多个条件串起来：

```cpp
int score = 85;

if (score >= 90) {
    std::cout << "等级: A" << std::endl;
} else if (score >= 80) {
    std::cout << "等级: B" << std::endl;
} else if (score >= 70) {
    std::cout << "等级: C" << std::endl;
} else if (score >= 60) {
    std::cout << "等级: D" << std::endl;
} else {
    std::cout << "等级: F" << std::endl;
}
```

运行结果：

```text
等级: B
```

这里有一个容易被忽略的细节：`else if` 并不是 C++ 的独立关键字，它实际上是 `else` 后面跟了一个新的 `if` 语句。编译器看到的是一棵嵌套的二叉分支树。条件是从上到下依次检查的，一旦某个条件为真，后面的所有分支都会被跳过——如果你把 `score >= 60` 写在 `score >= 90` 前面，那 85 分也会被归到 D 级去。另外，`if` 括号里的条件必须能转换成 `bool`：整数非零为 `true`，指针非空为 `true`。这个隐式转换后面会引出一个经典坑。

## 那些年我们踩过的坑——if 的常见陷阱

### 赋值 vs 比较——编译器不会拦你的笔误

```cpp
int x = 0;
if (x = 5) {
    std::cout << "x is 5" << std::endl;
}
```

你可能以为意思是"如果 x 等于 5"，但 `=` 是赋值运算符，`==` 才是比较运算符。这段代码做的事情是：把 5 赋给 `x`，然后因为赋值表达式的结果就是被赋的值（5，非零），条件永远为真。更糟糕的是，`x` 被意外修改成了 5。

> **踩坑预警**：`if (x = 5)` 能通过编译，不会报错，但逻辑几乎一定不是你想要的。务必开启 `-Wall -Wextra` 编译选项，GCC 和 Clang 遇到这种写法会发出警告。有些程序员习惯把常量写在左边 `if (5 == x)`，这样万一写成 `if (5 = x)` 编译器会直接报错，因为不能给常量赋值。

### 悬垂 else 和花括号的习惯

下面这段代码，缩进看起来 `else` 是和第一个 `if` 配对的：

```cpp
if (a > 0)
    if (b > 0)
        result = 1;
else
    result = -1;
```

但 C++ 的规则是 **`else` 总是和最近的、尚未配对的 `if` 绑定**。所以这段代码实际上等价于：

```cpp
if (a > 0) {
    if (b > 0) {
        result = 1;
    } else {
        result = -1;
    }
}
```

如果我们的本意是让 `else` 和外层 `if` 配对（`a <= 0` 时把 `result` 设为 -1），那这段代码就是完全错误的。

> **踩坑预警**：即使分支体只有一行代码，也要加花括号。这不是多打几个字符的问题，而是防止歧义和未来维护时引入 bug——你加一行代码的时候忘了补花括号，逻辑就完全变了。

## switch 语句——多路分支的利器

当你需要把同一个表达式和多个离散值做比较时，`switch` 比 `if/else if` 链更清晰。编译器通常还会把它优化成跳转表（jump table），查表接近 O(1)。

```cpp
enum class Command {
    kStart,
    kStop,
    kPause,
    kResume
};

void handle_command(Command cmd)
{
    switch (cmd) {
        case Command::kStart:
            std::cout << "启动操作" << std::endl;
            break;
        case Command::kStop:
            std::cout << "停止操作" << std::endl;
            break;
        case Command::kPause:
            std::cout << "暂停操作" << std::endl;
            break;
        case Command::kResume:
            std::cout << "恢复操作" << std::endl;
            break;
        default:
            std::cout << "未知命令" << std::endl;
            break;
    }
}
```

### 穿透特性——忘了 break 就会"漏水"

每个 `case` 末尾的 `break` 用来跳出 `switch`。如果忘了写，执行完当前 case 后不会停下来，而是"穿透"到下一个 case 继续执行——这就是 fall-through。比如当 `cmd` 为 `Command::kStart` 但忘了写 `break` 时，输出会是：

```text
启动
停止
```

一启动就停了，这就是穿透带来的 bug。

> **踩坑预警**：写 `switch` 必须写 `break`，这是铁律。养成习惯，每写完一个 `case` 就先写 `break` 再填充逻辑。如果你确实要利用穿透特性（比如把多个 case 合并到同一个处理逻辑），加个 `/* fall through */` 注释说明你的意图，否则后来维护代码的人会以为这是 bug。

### case 标签的限制

`switch` 的 case 标签必须是**整数常量表达式**——编译时就能确定值的整数，不能用变量、浮点数或字符串。另外，养成写 `default` 分支的习惯，哪怕它只是打一行日志。特别是当你的枚举后来新增了成员但忘了更新 `switch` 的时候，`default` 就是你的安全网。

## 三元运算符——简洁的条件表达式

三元运算符的语法是 `condition ? value_if_true : value_if_false`，它是 `if/else` 的一种表达式形式，适合在两种值之间做选择：

```cpp
int a = 10;
int b = 20;
int max_val = (a > b) ? a : b;  // max_val = 20
```

三元运算符能直接嵌入表达式中，在初始化 `const` 变量时特别有用——`const` 只能初始化不能赋值，用 `if/else` 就做不了：

```cpp
const int kBufferSize = (mode == Mode::kHighSpeed) ? 1024 : 256;
```

但三元运算符不适合嵌套。像 `a ? b ? c : d : e` 虽然语法合法，可读性极差。逻辑超过两层选择，老老实实写 `if/else`。

## C++17：带初始化器的 if 和 switch

C++17 引入了一个很实用的特性——在 `if` 和 `switch` 的条件部分可以放一个初始化语句，用分号和条件表达式隔开：

```cpp
if (int x = compute_value(); x > 0) {
    std::cout << "正数: " << x << std::endl;
} else {
    std::cout << "非正数: " << x << std::endl;
}
// x 在这里已经不可见了
```

初始化语句中声明的变量在整个 `if/else` 范围内可见，语句结束后就离开作用域。以前你可能需要在 `if` 之前声明一个临时变量，然后它就一直活着直到函数结束——这个特性让作用域更紧凑，变量用完即销毁。

`switch` 也支持同样的写法：

```cpp
switch (auto cmd = parse_command(input); cmd) {
    case Command::kStart:
        start_operation();
        break;
    case Command::kStop:
        stop_operation();
        break;
    default:
        handle_unknown(cmd);
        break;
}
```

`cmd` 的作用域被限制在 `switch` 内部，不会泄漏到外面。

## 实战演练——conditional.cpp

现在我们把这一章学的东西整合到一个完整程序里：根据输入的成绩分数输出等级，用不同方式实现。

```cpp
#include <iostream>

/// @brief 用 if-else 链判断成绩等级
/// @param score 百分制分数 (0-100)
/// @return 等级字符
char grade_by_if(int score)
{
    if (score >= 90) {
        return 'A';
    } else if (score >= 80) {
        return 'B';
    } else if (score >= 70) {
        return 'C';
    } else if (score >= 60) {
        return 'D';
    } else {
        return 'F';
    }
}

/// @brief 用 switch 判断成绩等级
/// @param score 百分制分数 (0-100)
/// @return 等级字符
char grade_by_switch(int score)
{
    switch (score / 10) {
        case 10:
        case 9:
            return 'A';
        case 8:
            return 'B';
        case 7:
            return 'C';
        case 6:
            return 'D';
        default:
            return 'F';
    }
}

int main()
{
    int score = 0;
    std::cout << "请输入成绩 (0-100): ";
    std::cin >> score;

    if (score < 0 || score > 100) {
        std::cout << "无效的成绩输入" << std::endl;
        return 1;
    }

    char grade = grade_by_if(score);
    std::cout << "if-else 判定结果: " << grade << std::endl;

    grade = grade_by_switch(score);
    std::cout << "switch 判定结果:  " << grade << std::endl;

    std::cout << "是否及格: "
              << (score >= 60 ? "是" : "否") << std::endl;

    if (int diff = score - 60; diff >= 0) {
        std::cout << "超过及格线 " << diff << " 分" << std::endl;
    } else {
        std::cout << "距离及格还差 " << -diff << " 分" << std::endl;
    }

    return 0;
}
```

编译运行：

```bash
g++ -std=c++17 -Wall -Wextra -o conditional conditional.cpp
./conditional
```

测试输入 85：

```text
请输入成绩 (0-100): 85
if-else 判定结果: B
switch 判定结果:  B
是否及格: 是
超过及格线 25 分
```

测试输入 42：

```text
请输入成绩 (0-100): 42
if-else 判定结果: F
switch 判定结果:  F
是否及格: 否
距离及格还差 18 分
```

很好，三种条件语句都给出了正确一致的结果。注意 `grade_by_switch` 利用 `score / 10` 把分数映射到 0-10，再用穿透特性合并 10 和 9。这种技巧在实际项目中偶尔能见到，但如果你觉得不好读，用 `if-else` 链也没问题，可读性优先。

## 动手试试

光看不练等于没学。以下是三个练习，难度递增，建议每个都动手写一遍。

### 练习一：正数、负数、零

写一个程序，读取一个整数，判断它是正数、负数还是零。要求分别用 `if-else` 链和三元运算符两种方式实现。

预期交互效果：

```text
请输入一个整数: -7
-7 是负数
```

### 练习二：简单计算器

用 `switch` 实现一个简单计算器：从标准输入读取两个整数和一个运算符（`+`、`-`、`*`、`/`），输出运算结果。除法要处理除数为零的情况。

预期交互效果：

```text
请输入表达式（如 3 + 5）: 10 / 0
错误：除数不能为零
```

### 练习三：日期合法性检查

写一个函数，接收年、月、日三个整数，用条件语句判断这个日期是否合法。需要考虑月份范围是否在 1-12、每月天数上限不同、闰年的二月有 29 天。提示：用 `switch` 处理不同月份的天数会非常清晰。

## 小结

条件语句是程序逻辑的骨架。`if/else` 是最通用的分支手段，`switch` 适合对离散值做多路匹配，三元运算符适合在表达式中做简单的二选一，C++17 的带初始化器 `if` 让作用域控制更精确。永远用花括号包裹分支体，`=` 和 `==` 千万别搞混，`switch` 的每个 `case` 都要写 `break`，三元运算符不要嵌套。这些看似简单但在实际项目中反复出现的坑，从第一天就养成好习惯，后面的路会好走很多。

下一章我们学习循环语句——让程序学会重复。循环和条件组合在一起，就构成了图灵完备的计算能力，任何可计算的问题都能用它们来表达。
