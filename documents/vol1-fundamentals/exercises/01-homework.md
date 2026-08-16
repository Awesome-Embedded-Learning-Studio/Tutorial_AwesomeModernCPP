---
title: "卷 1 · 基础 课后练习（Homework）"
description: "卷 1（C++ 基础入门）课后练习：ch00~ch12 每章 2 题（基础+进阶），c_tutorials 速成 2 题，另加 2 道跨章综合与 1 道 L5 挑战（接雨水，LeetCode 42 Hard 改编）。难度覆盖 L1~L5，题目全部做了变式处理——换场景、换数据、换推理方向，照抄教材例题抄不出答案。参考答案独立成文件，逐步解答附知识点链接。"
chapter: 1
order: 1
tags: [host, beginner, cpp-modern, 基础]
difficulty: beginner
platform: host
cpp_standard: [11, 14, 17]
reading_time_minutes: 25
prerequisites: []
related: []
---

# 卷 1 · 基础 课后练习（Homework）

## 引言

本卷的教材把 C++ 的地基铺得很实在——从环境搭建一路走到内存模型。但「看懂」和「写得出来」之间隔的那段路，只能靠你自己用键盘走出来。这里的题按章组织，每章两道（基础 + 进阶），最后是两道跨章综合和一道 L5 挑战。每题标注难度档位（L1~L5，档位口径见[练习总览](./index.md)）和涉及章节，题目全部做了「变式」处理：换场景、换数据、换方向，照抄教材例题是抄不出答案的。

做题的铁律和教材一样：每道题都要真编译、真运行，把输出贴下来才算完。答案在独立的[参考答案](./02-homework-solutions.md)文件里，按题号一一对应，每步解答带知识点链接。建议一章做完再看答案。默认编译命令 `g++ -std=c++17 -Wall -Wextra` 起步，个别题目会要求 sanitizer 或 C 编译器，题面会写明。卡住的时候先回题面标注的章节链接读教材——比直接翻答案快。

## 0.1 环境搭建与第一个程序

### 0.1-A {#hw-0-1-a}

难度 **L1** · 涉及[第一个 C++ 程序](../ch00/03-first-program.md)

教材带我们写了摄氏转华氏（$F = \frac{C \times 9}{5} + 32$）。现在反过来：写一个 `fahrenheit_to_celsius.cpp`，读入华氏温度（支持小数），用 $C = \frac{(F - 32) \times 5}{9}$ 换算成摄氏并输出，格式如 $77°F = 25°C$（保留 1 位小数）。再补一段「反向换算表」：固定打印 $-10 / 0 / 10 / 25 / 37$ 这五个摄氏温度对应的华氏温度。思考：公式里的 $32.0$、$5.0$、$9.0$ 为什么写成浮点字面量（输入是 `double`，写成整数版结果其实一样——想想为什么）？整数版换算表会出什么事（用 37° 试）？先预测再验证。

[参考答案 →](./02-homework-solutions.md#hw-0-1-a)

### 0.1-B {#hw-0-1-b}

难度 **L2** · 涉及[第一个 C++ 程序](../ch00/03-first-program.md)（编译流水线一节）、[Linux 环境搭建](../ch00/01-setup-linux.md)

写四个「坏文件」，每个只埋一种错，分别编译并判断错误发生在编译流水线的哪个阶段：

1. `bad_preprocess.cpp`：`#include` 一个不存在的头文件；
2. `bad_compile.cpp`：`std::cout << "hello" << std::endl` 那一行末尾漏了分号；
3. `bad_include.cpp`：干脆不写 `#include <iostream>`；
4. `bad_link.cpp`：声明了 `int helper(int x);` 并且 `main` 里调用了它，但整个程序里没有它的定义。

每编译一个，贴出真实报错，说清它卡在「预处理 / 编译 / 汇编 / 链接」哪一段。最后做一个「双错合并」实验：把 `#include` 删掉、分号也删掉放在同一个文件里，观察编译器报出的第一条错误是哪一条——结合教材「看第一条、修第一条、重新编译」的忠告，说说为什么不要试图一次看懂所有报错。

[参考答案 →](./02-homework-solutions.md#hw-0-1-b)

## 1.1 类型与值类别

### 1.1-A {#hw-1-1-a}

难度 **L1** · 涉及[基本数据类型](../ch01/01-basic-types.md)

**先动笔预测**下面 8 个表达式的结果，再写程序真跑验证：`sizeof(char)`、`sizeof(c + c)`（`c` 是 `char`）、`sizeof('A')`、`sizeof(true)`、`sizeof(3.14)`、`sizeof(3.14f)`、`sizeof("abc")`、`sizeof(flag)`（`flag` 是 `bool`）。跑完再补两行：用 `std::numeric_limits<char>::is_signed` 和取值范围判断本机 `char` 是有符号还是无符号。对答案的时候重点想三个问题：`sizeof('A')` 为什么是 1 而不是 4（对比一下 C 里字符常量的类型，可以做一遍 [基本数据类型](../ch01/01-basic-types.md)的练习二——它正好让你对比 C 和 C++ 两门语言里 `sizeof('A')` 的差异）；`sizeof(c + c)` 里的 4 是哪来的；「char 有符号」这个事实能不能写进你的跨平台代码假设里？

[参考答案 →](./02-homework-solutions.md#hw-1-1-a)

### 1.1-B {#hw-1-1-b}

难度 **L2** · 涉及[类型转换](../ch01/02-type-conversion.md)

三个小场景，都要先预测再真跑。①利息计算：本金 $9999$ 元、年利率 `3.3%`（用千分位整数 $33$ 存储，嵌入式里常见的定点手法）、存 `3` 年。写两版利息计算——错版 `principal * rate_milli / 1000 * years`，对版把 `rate_milli` 先用 `static_cast<double>` 转掉。贴出两个结果，说清那 $2.901$ 元是在哪一步被整数除法截掉的。②有符号与无符号：打印 `(-1 < 1u)` 的真假，再打印 `static_cast<unsigned int>(-1)` 的值——用 `-Wsign-compare` 编译，把警告也贴下来，解释「负数怎么在比较里变成了巨正数」。③除法和取余的方向：打印 $\frac{-7}{2}$、`-7 % 2`、$\frac{7}{-2}$、`7 % -2`，解释 C++ 整数除法「向零取整」和「余数符号跟随被除数」这两条规则。

[参考答案 →](./02-homework-solutions.md#hw-1-1-b)

## 2.1 控制流

### 2.1-A {#hw-2-1-a}

难度 **L1** · 涉及[条件语句](../ch02/01-conditionals.md)

教材的成绩等级判定已经写透了。换个场景：写一个「体温分诊」判定——`>= 37.3` 判定为发热，`>= 36.0` 判定为正常，其余判定为偏低。用 `if / else if / else` 链实现一个函数，对 `{35.8, 36.5, 37.3, 38.9}` 四个读数逐一判定并打印；再任选一个读数用**三元运算符**把同一套判定重写一遍，对比两种写法各自适合什么场合。特别留意边界值 $37.3$：它应该落进哪个分支？把 `>=` 写成 `>` 会怎样？

[参考答案 →](./02-homework-solutions.md#hw-2-1-a)

### 2.1-B {#hw-2-1-b}

难度 **L2** · 涉及[循环语句](../ch02/02-loops.md)

教材练习四是实心菱形，现在上空心版：写一个程序，输入固定的奇数 `N = 7`，用**嵌套 for 循环**打印空心菱形——只有每行星号层的首尾两个位置是 `*`，中间全是空格。提示：第 `row` 行离中线的距离 `d = |row - N/2|`，该行星号层的宽度是 `2 * (N/2 - d) + 1`，先打 `d` 个前导空格再打两个星号夹着 `width - 2` 个空格（宽度为 1 的顶行和底行只打一个星号）。先拿纸笔手动推演两行，再上机对答案。

[参考答案 →](./02-homework-solutions.md#hw-2-1-b)

## 3.1 函数

### 3.1-A {#hw-3-1-a}

难度 **L1** · 涉及[函数基础](../ch03/01-function-basics.md)

两道小题。①用**递归**实现 `int power(int base, int exp)`（幂运算），基线条件是 `exp == 0` 返回 1；对 `exp < 0` 的契约外输入返回约定值 $-1$。验证 `power(2, 10) == 1024`、`power(5, 0) == 1`、`power(2, -1) == -1`。②用**迭代**实现 `int collatz_steps(long long n)`：n 为偶数就除 2、为奇数就算 $3n + 1$，数一数走到 1 需要多少步（这就是著名的考拉兹猜想序列）。验证 `collatz_steps(27) == 111`、`collatz_steps(6) == 8`、`collatz_steps(1) == 0`。想一下：为什么 `n` 要用 `long long` 而不是 `int`？（提示：跑一遍 27 的过程值，看它中间最大蹦到多少。）

[参考答案 →](./02-homework-solutions.md#hw-3-1-a)

### 3.1-B {#hw-3-1-b}

难度 **L2** · 涉及[参数传递方式](../ch03/02-pass-by-value-ref.md)

写三件套。①`void swap_bad(int a, int b)`（值传递）和 `void swap_ok(int& a, int& b)`（引用传递），在 `main` 里先调用 `swap_bad(x, y)` 再调用 `swap_ok(x, y)`，打印每次调用后的 `x`、`y`，说清值传递为什么改不动调用者的变量。②写 `void min_max(double a, double b, double c, double& mn, double& mx)`，用两个引用参数把最小值和最大值「带出来」，验证 `min_max(3.5, -1.2, 8.8, ...)` 得到 $-1.2$ 和 $8.8$——这里为什么必须用引用参数，而没法用返回值？（提示：一个函数只能 return 一个值，除非像教材那样打包成 `struct`。）③思考题：`min_max(1.0, 2.0, 3.0, 0.0, 0.0)` 这样传两个字面量当输出参数，能编译吗？为什么？（结合[值类别简介](../ch01/04-value-categories.md)里非 const 引用不能绑定右值的规则。）

[参考答案 →](./02-homework-solutions.md#hw-3-1-b)

## 4.1 指针与引用

### 4.1-A {#hw-4-1-a}

难度 **L2** · 涉及[指针基础](../ch04/01-pointer-basics.md)

教材练习二给了指针追踪题，这里换数据再战一次。**先不动笔运行**，在纸上追踪下面这段程序的每一步输出：`int x = 5, y = 9;`，`int* p = &x; int* q = &y;`，然后依次执行「`*p = *q;` → 打印 x、y → `p = q;` → `*p = 20;` → 打印 x、y → 打印 `*p`、`*q`」。重点分辨 `*p = *q`（赋值**数据**）和 `p = q`（改变**指向**）这两行的区别——很多人第一次做都栽在这里。最后补一段空指针安全打印：声明 `int* null_ptr = nullptr;`，先判空再决定是否解引用，贴出完整输出并和你的预测对照。

[参考答案 →](./02-homework-solutions.md#hw-4-1-a)

### 4.1-B {#hw-4-1-b}

难度 **L2** · 涉及[指针运算与数组](../ch04/02-pointer-arithmetic.md)

纯指针实现两件事，不许用下标。①手写 `std::size_t my_strlen(const char* s)`：从字符串头部出发，用指针自增走到 `'\0'`，用指针减法算出长度。②手写 `void reverse_in_place(char* s)`：用「一头一尾两个指针向中间走、边走边交换」的经典双指针套路，把 C 字符串原地反转。在 `main` 里对 `char text[] = "modern cpp";` 先打印原文和长度、反转、再打印。最后加一个观察项：声明 `const char* literal = "readonly";`，用 `static_cast<const void*>` 分别打印 `literal` 和 `text` 的地址，对比两个地址的数值量级，结合[内存布局](../ch12/01-memory-layout.md)说说为什么一个落在只读段、一个落在栈上。

[参考答案 →](./02-homework-solutions.md#hw-4-1-b)

## 5.1 数组与字符串

### 5.1-A {#hw-5-1-a}

难度 **L1** · 涉及[std::array](../ch05/02-std-array.md)

把教材的成绩统计换成气象场景：`std::array<double, 8>` 存 8 个温度读数 `{23.1, 19.4, 26.7, 18.2, 25.0, 21.8, 27.3, 22.6}`。用 `std::minmax_element` 一趟找出最低和最高，用 range-for 累加求平均。然后做一步「原地修正」：把低于 20 度的读数都加 1.5 度——这里 range-for 必须用哪种形式（`auto` / `auto&` / `const auto&`）？如果用了按值的形式，会发生什么「编译通过、运行不报错、但结果不对」的事？修正后再统计一次最低最高，贴出两组完整输出。

[参考答案 →](./02-homework-solutions.md#hw-5-1-a)

### 5.1-B {#hw-5-1-b}

难度 **L2** · 涉及[std::string](../ch05/03-std-string.md)

写两个函数，不许用 `std::istringstream`，全用 `find`/`substr` 那套。①`std::string longest_word(const std::string& text)`：按空格切词（假设单词之间恰好一个空格、无首尾空格），返回最长的一个；长度并列时返回先出现的。对 `"modern cpp standard library strings"` 验证——你预测最长单词是哪个？先猜再跑。②`std::string replace_all(std::string text, const std::string& from, const std::string& to)`：把 `text` 里所有 `from` 替换成 `to`，把 `"modern cpp standard library strings"` 里的 `"modern"` 换成 `"awesome"`。特别要处理 `from` 为空串的情况（直接返回原文）——想清楚为什么：如果 `from` 为空，`find` 每次都会返回什么？循环会发生什么？最后用 `find` 查一个不存在的子串，验证返回值等于 `std::string::npos`。

[参考答案 →](./02-homework-solutions.md#hw-5-1-b)

## 6.1 类与面向对象

### 6.1-A {#hw-6-1-a}

难度 **L1** · 涉及[类的定义](../ch06/01-class-basics.md)

写一个 `Circle` 类：私有成员 `double radius_`；构造函数把负半径钳制为 0；公有成员函数 `area()`（πr²）、`perimeter()`（2πr）、`scale(double factor)`（正因子才放大）、`contains(double x, double y) const`（判断点是否落在圆内，用 `sqrt(x² + y²) <= radius`）、`print() const`。在 `main` 里验证：半径 3 的圆面积和周长（对比手算 9π 和 6π）；`(2,2)` 在圆内而 `(2.2,2.2)` 不在；`scale(2.0)` 后面积变成 4 倍；构造 `Circle(-5.0)` 时负半径被钳成 0。想一想：`scale` 为什么没有加 `const`，而 `area` 和 `contains` 加了？少了 `const` 会拦住哪些用法？

[参考答案 →](./02-homework-solutions.md#hw-6-1-a)

### 6.1-B {#hw-6-1-b}

难度 **L2** · 涉及[构造函数](../ch06/02-constructors.md)、[static 成员](../ch06/04-static-members.md)

写一个带自动编号的 `BankAccount` 类：私有成员 `int id_`、`std::string owner_`、`double balance_`，加上两个静态成员 `static int next_id_`（从 1001 起）和 `static int active_count_`。构造函数从 `next_id_` 取号自增、把负数余额归零、`active_count_` 加一；析构函数把 `active_count_` 减一；再补一个**委托构造**的无参版本（委托成 `BankAccount("匿名", 0.0)`）。公有接口：`deposit(double)` 和 `withdraw(double)`（金额非正返回 false、提款超额返回 false）、`id()`/`balance()`/`owner()` 三个只读访问器、静态函数 `active_count()` 和 `next_id()`。验证：创建 Alice（500）、Bob（100）和一个匿名账户，打印三人编号和余额；提款 600 失败、存款 -50 失败；花括号作用域里建一个临时账户，观察进作用域前后 `active_count()` 的变化和 `next_id()` 只增不减。注意静态成员要在类外定义——想想 C++17 的 `inline static` 能不能省掉这一步。

[参考答案 →](./02-homework-solutions.md#hw-6-1-b)

## 7.1 运算符重载

### 7.1-A {#hw-7-1-a}

难度 **L3** · 涉及[算术与比较运算符](../ch07/01-arithmetic-comparison.md)

教材拿 `Fraction` 打了样，现在做一个 `TimeSpan` 时长类：内部只存一个 `long total_seconds_`；成员函数 `hours()`/`minutes()`/`seconds()` 把它拆成时分秒；按教材的模式实现 `operator+=`、`operator-=`（成员函数，返回 `*this` 引用），再基于它们实现非成员的 `operator+`、`operator-`（左侧按值接收），以及 `==`、`!=`、`<` 和输出运算符 `<<`（格式 `1h1m11s`）。验证：`TimeSpan(3671) + TimeSpan(125)` 是 `1h3m16s`，$3671 - 125$ 是 `0h59m6s`，两个相同秒数判等成立。回答两问：为什么 `operator+` 的左侧操作数按值传而右侧按 const 引用传？为什么 `==` 和 `<` 都直接比较 `total_seconds()` 而不去比较拆出来的时分秒？（想想「单一真相源」和字段之间的一致性。）

[参考答案 →](./02-homework-solutions.md#hw-7-1-a)

### 7.1-B {#hw-7-1-b}

难度 **L3** · 涉及[流与下标运算符](../ch07/02-io-subscript.md)

教材练习二是用代理类实现二维下标。现在实现一个 `Matrix3`：内部用 `std::array<double, 9>` 按行优先存 3×3 矩阵；重载 `operator[]` 返回一个 `Row` 代理对象，`Row` 再重载自己的 `operator[]` 把 `[row][col]` 翻译成一维下标——这样 `m[1][2] = 6.0;` 和 `std::cout << m[1][2];` 才能正常工作。要求 `Row` 同时提供非 const 和 const 两个版本的 `operator[]`，而且 `Matrix3` 也要提供 const 版本的 `operator[]`（可以再写一个只读的 `ConstRow` 代理，或者想办法让一个代理通吃——两种路线都可以，写清楚你选哪条）。补两个功能：`double trace() const`（对角线之和）和 `void print() const`。验证：把 1~9 依次填入矩阵，迹应为 15；通过 `const Matrix3&` 只读访问 `m[1][2]`；改写 `m[2][0]` 后重新打印。最后回答：`m[1][2]` 这个表达式在编译器眼里到底经历了哪两次函数调用？为什么 `operator[]` 不能直接返回 `double&`？

[参考答案 →](./02-homework-solutions.md#hw-7-1-b)

## 8.1 继承与多态

### 8.1-A {#hw-8-1-a}

难度 **L2** · 涉及[单继承](../ch08/01-single-inheritance.md)

写一个 `Vehicle` 基类：`protected` 的 `std::string brand_`，构造函数和析构函数各打印一行（带上品牌），虚函数 `virtual std::string describe() const` 返回品牌。派生 `Car`（加 `int seats_`，重写 `describe` 返回 `品牌 (N 座)`）和 `Bicycle`（重写 `describe` 返回 `品牌 (两轮)`），两个派生类的构造/析构也各打印一行。再写两个观察函数：`void show_by_value(Vehicle v)` 和 `void show_by_ref(const Vehicle& v)`，都调用 `describe()`。在 `main` 里按顺序做：花括号里构造一辆四座 Car 和一辆 Bicycle，先后用引用和按值两种方式观察它们——贴出完整输出，回答：①构造顺序和析构顺序是什么规律？②`show_by_value(car)` 的输出丢了什么？为什么？③按值传参时多出来的那对「[Vehicle] 构造/析构」打印是怎么回事？④`describe` 已经是虚函数了，为什么按值传还是会调基类版本？

[参考答案 →](./02-homework-solutions.md#hw-8-1-a)

### 8.1-B {#hw-8-1-b}

难度 **L3** · 涉及[虚函数与多态](../ch08/02-virtual-functions.md)、[智能指针预告](../ch04/04-smart-ptr-preview.md)

实现一个发工资系统。抽象基类 `Employee`：`protected` 存 `std::string name_`，纯虚函数 `virtual double salary() const = 0`，虚析构 `virtual ~Employee() = default`。三个派生类：`SalariedEmployee`（月薪）、`HourlyEmployee`（时薪 × 月工时）、`CommissionEmployee`（底薪 + 提成）。在 `main` 里用 `std::vector<std::unique_ptr<Employee>>` 装三个员工（Alice 月薪 12000、Bob 时薪 45.5 × 160 小时、Carol 底薪 3000 加提成 2500），range-for 遍历打印每人姓名和工资，最后打印总支出（应为 24780）。回答三问：为什么容器里放的是 `unique_ptr<Employee>` 而不是 `Employee` 值？（两个原因：切片 + 抽象类不可实例化。）为什么基类析构函数必须 `virtual`？将来要加一个 `PieceWorker`（计件工）类，主循环的代码要不要改？

[参考答案 →](./02-homework-solutions.md#hw-8-1-b)

## 9.1 模板初步

### 9.1-A {#hw-9-1-a}

难度 **L2** · 涉及[函数模板](../ch09/01-function-templates.md)

写一个泛型数组反转模板：`template <typename T, std::size_t kN> void array_reverse(T (&arr)[kN])`——用双指针或双下标原地反转。再写一个配套的 `array_print` 模板打印数组。用三组数据验证：`int[]{3, 1, 4, 1, 5}`、`double[]{22.5, 23.1, 19.8}`、`std::string[]{"Mon", "Tue", "Wed", "Thu"}`，每组先打印、反转、再打印。回答：①参数写成 `T (&arr)[kN]` 而不是 `T arr[]` 的意义是什么？（编译器凭什么能推导出 kN？）②为什么参数必须是引用，改成按值会怎样？③这次总共实例化了多少个 `array_reverse` 版本？（结合「模板实例化」机制。）

[参考答案 →](./02-homework-solutions.md#hw-9-1-a)

### 9.1-B {#hw-9-1-b}

难度 **L3** · 涉及[类模板](../ch09/02-class-templates.md)

教材练习二是环形缓冲区，这里把语义升级成嵌入式场景：`template <typename T, std::size_t kCapacity> class RingBuffer`，内部用 `std::array<T, kCapacity>`，提供 `push(const T&)`、`bool pop(T& out)`、`empty()`、`full()`、`size()` 和 `print()`。关键语义：**缓冲区满时 push 覆盖最旧的元素**（而不是拒绝写入——传感器数据流常用这个语义，最新的读数永远比旧的重要）。验证：容量 4 的 `RingBuffer<int, 4>` 依次 push 1、2、3（打印），再 push 4、5（打印）——观察 1 被谁顶掉了；然后循环 pop 直到空，打印弹出序列。再用 `RingBuffer<double, 3>` 验证一次。回答：①非类型模板参数 `kCapacity` 在这里起了什么作用？②为什么只需要 `count_` 和 `read_index_` 两个变量就够定位所有元素，不需要单独存「写位置」？

[参考答案 →](./02-homework-solutions.md#hw-9-1-b)

## 10.1 异常处理

### 10.1-A {#hw-10-1-a}

难度 **L2** · 涉及[异常基础](../ch10/01-try-catch.md)

写 `double safe_divide(double dividend, double divisor)`：除数为零时 `throw std::invalid_argument("除数为零")`。在 `main` 里对三组输入 `{10.0, 4.0}`、`{7.0, 0.0}`、`{-8.0, 2.0}` 分别 try/catch，正常打印结果、异常打印 `e.what()`。再补一段「层次化捕获」实验：`std::vector<int> v = {10, 20, 30};` 调 `v.at(5)` 触发越界，依次写三个 catch 块——`std::out_of_range`、`std::logic_error`、`std::exception`——贴出运行结果，回答：①为什么 `at(5)` 会抛异常而 `v[5]` 不会？②三个 catch 的顺序为什么必须是「具体在前、一般在后」？③为什么把 `e.what()` 的返回值存下来、在 catch 块外面再用，是危险行为？

[参考答案 →](./02-homework-solutions.md#hw-10-1-a)

### 10.1-B {#hw-10-1-b}

难度 **L3** · 涉及[异常安全](../ch10/02-exception-safety.md)

这是教材「裸指针 vs unique_ptr」对比实验的加强版，请务必跑 ASan。写一个 `risky_step(bool fail)`（fail 时抛 `std::runtime_error`）和两个处理函数：`process_bad(bool fail)` 里先 `new int[1024]`、再 `new double[512]`、然后调 `risky_step(fail)`、最后才 `delete[]` 两个指针；`process_good(bool fail)` 里用两个 `std::make_unique` 数组版本做同样的事。在 `main` 里分别 try/catch 调用两个函数（都传 `true`）。实验要求：①两个函数各用普通构建跑一遍，观察输出差异；②两个函数各用 `-fsanitize=address,undefined` 构建跑一遍，**贴出坏版的 LeakSanitizer 报告**（应报告两块各 4096 字节的泄漏，合计 8192 字节）和好版的零报告；③回答：异常飞过时，坏版的两个 `delete[]` 为什么永远执行不到？`unique_ptr` 凭什么就能在异常路径上保证释放？（回到 RAII 那句老话：资源生命周期绑定到对象生命周期上。）

[参考答案 →](./02-homework-solutions.md#hw-10-1-b)

## 11.1 STL 初见

### 11.1-A {#hw-11-1-a}

难度 **L2** · 涉及[std::vector 快速上手](../ch11/01-vector.md)

写一条「传感器读数清洗管道」：`std::vector<int> readings = {23, 21, 25, -5, 22, 99, 24, 20};`——合理范围 `[0, 60]`，之外的算异常。①用 **remove-erase 惯用法**删掉异常值；②`std::sort` 升序排序；③打印清洗排序后的序列、平均分（注意除法前转 double）、以及 `size` 和 `capacity`。贴出输出并回答：为什么 `erase` 之后 `capacity` 还是 8？（回到 `size` 和 `capacity` 的区分。）为什么用 `remove_if + erase` 两步走，而不是在 range-for 里直接 `erase`？

[参考答案 →](./02-homework-solutions.md#hw-11-1-a)

### 11.1-B {#hw-11-1-b}

难度 **L3** · 涉及[关联容器快速上手](../ch11/02-map-set.md)、[算法库初见](../ch11/03-algorithms-intro.md)

词频统计升级版。对句子 `"the quick brown fox jumps over the lazy dog the fox"`：①用 `std::map<std::string, int>` 统计每个词的出现次数（这里用 `++freq[word]` 是合理的——说清为什么这个场景里 `operator[]` 的「自动插入」恰好是我们想要的）；②打印按字典序排列的完整词频表；③统计 **Top 3 高频词**：把 map 的内容搬进 `std::vector<std::pair<std::string, int>>`，按次数降序排序——这里必须用 `std::stable_sort` 而不是 `std::sort`，为什么？（提示：并列次数的词，输出顺序你希望是稳定的字典序。）贴出词频表和 Top 3。

[参考答案 →](./02-homework-solutions.md#hw-11-1-b)

## 12.1 内存模型基础

### 12.1-A {#hw-12-1-a}

难度 **L2** · 涉及[内存布局](../ch12/01-memory-layout.md)

把教材的 layout 实验做完整：声明全局变量 `int g_initialized = 42;` 和 `int g_uninitialized;`，写一个子函数 `probe_stack()`（内部声明一个局部变量并打印其地址），在 `main` 里声明 `static int s_local = 3;`、栈上 `int stack_var = 1;`、堆上 `new int(2)`。逐一打印：`main` 函数地址（代码段）、两个全局变量、static 局部、栈变量、堆变量、子函数栈变量，再打印「栈地址 > 堆地址」「栈地址 > 数据段地址」两个比较结果。贴出输出，对照本机实际地址解释四大区域的相对位置和栈向下增长的证据。最后做几道判断题：`const char* msg = "error";` 里的 `msg` 指针和 `"error"` 字面量各住在哪个区域？函数内的 `static int visits` 呢？

[参考答案 →](./02-homework-solutions.md#hw-12-1-a)

### 12.1-B {#hw-12-1-b}

难度 **L3** · 涉及[内存对齐与填充](../ch12/03-alignment-padding.md)

**先不动手编译**，在纸上预测以下两个结构体的 `sizeof` 和每个成员的偏移量，画出 padding 分布图，再写程序用 `offsetof` 验证：

```cpp
struct SensorRecord {
    char tag;       // 1 字节
    double sample;  // 8 字节
    int id;         // 4 字节
    short gain;     // 2 字节
};

struct SensorRecordTight {
    double sample;
    int id;
    short gain;
    char tag;
};
```

两个结构体字段完全相同、只有声明顺序不同。预测 `sizeof` 差多少？再补一个 `struct PacketHeader { char version; char flags; int sequence; double timestamp; };` 预测它的布局。验证后总结三条规则：成员对齐到自身对齐值的整数倍偏移、结构体大小是最大成员对齐值的整数倍、把大对齐成员放前面可以省掉多少 padding。如果这些结构体要被 `memcpy` 进网络报文，你该对字段顺序注意什么？

[参考答案 →](./02-homework-solutions.md#hw-12-1-b)

## CT C 语言速成回顾（c_tutorials）

### CT-1-A {#hw-ct-1-a}

难度 **L2** · 涉及[位运算与求值顺序](../c_tutorials/03B-bitwise-and-evaluation.md)

用 **C11**（`gcc -std=c11 -Wall -Wextra`）写一个位运算小实验：`uint8_t reg` 上依次做——用 `|= (1u << n)` 置位第 2、第 0 位并打印十六进制和二进制形式；用 `& (1u << n)` 检查第 2 位为 1、第 1 位为 0；用 `^= (1u << 0)` 翻转第 0 位；用 `&= ~(1u << 2)` 清零第 2 位。每一步都打印 `0x%02X` 和 8 位二进制。再做一次拆包和组包：把 `0xB4` 拆成高 4 位和低 4 位分别打印，再把高 4 位 `0x7` 和低 4 位 `0x9` 组回一个字节。贴出全部输出，说清拆包为什么「先移位再掩码」、组包为什么「先移位再或」。

[参考答案 →](./02-homework-solutions.md#hw-ct-1-a)

### CT-1-B {#hw-ct-1-b}

难度 **L3** · 涉及[C 字符串与缓冲区安全](../c_tutorials/11-c-strings-and-buffer-safety.md)

用 **C11** 写三件事。①手写 `size_t my_strlen(const char* s)`（指针自增版），对 `"hello"` 验证得 5。②安全拷贝实验：`char dst[8];` 用 `strncpy` 拷贝 `"Modern C++ Journey"`，拷完手动补 `'\0'`，打印结果和长度——观察 gcc 编译时有没有对 `strncpy` 截断给出警告；再用 `snprintf` 把 28 字符的字符串写进 20 字节缓冲，用**返回值**判断是否截断并打印警告。③埋雷实验：`char small[5]; strcpy(small, "this string is way too long");` 分别用普通构建和 `-fsanitize=address,undefined` 构建运行，贴出两份报告——普通构建下发生了什么（观察退出码和报错文本）？ASan 报告里点名的「WRITE of size」和越界变量是哪个？

[参考答案 →](./02-homework-solutions.md#hw-ct-1-b)

## C 跨章综合与挑战

### C-1 {#hw-c-1}

难度 **L4** · 涉及[std::string](../ch05/03-std-string.md)、[算术与比较运算符](../ch07/01-arithmetic-comparison.md)、[std::vector 快速上手](../ch11/01-vector.md)、[异常基础](../ch10/01-try-catch.md)

综合题：写一个「分数阅读器」。按教材实现一个自动约分的 `Fraction` 类（`+=`、`/=` 成员运算符，非成员的 `+`、`/`、`<`、`<<`）；再写 `Fraction parse_fraction(const std::string& text)`：用 `find` 找 `'/'`，`substr` 切开分子分母，`std::stoi` 转整数——**缺分隔符抛 `std::invalid_argument`、分母为零抛 `std::invalid_argument`**。在 `main` 里解析 `{"3/4", "1/2", "2/3", "7/6", "5/12"}` 五个字符串，再故意解析一个 `"1/0"` 验证异常被 catch；把合法分数放进 `std::vector<Fraction>`，用 `std::sort` 按 `operator<` 排序打印；用 `std::accumulate`（初值 `Fraction(0, 1)`，二元操作用 lambda 返回 `acc + f`）求总和；总和除以个数得到平均分并打印。手算验证：这五个分数排序后是什么顺序？总和是多少？平均是多少？（答案见参考文件，先自己通分算一遍。）

[参考答案 →](./02-homework-solutions.md#hw-c-1)

### C-2 {#hw-c-2}

难度 **L4** · 涉及[值类别简介](../ch01/04-value-categories.md)、[单继承](../ch08/01-single-inheritance.md)、[std::vector 快速上手](../ch11/01-vector.md)、[STL 常用模式](../ch11/04-stl-patterns.md)

机制分析题：写一个程序验证本卷的三个「隐形规则」，每步先预测再真跑。①**生命周期延长**：`const std::string& label = std::string("sensor-") + std::to_string(7);` 之后打印 `label`——合法吗？为什么 const 引用能「续命」临时对象，而非 const 引用不行？②**对象切片**：基类 `Shape` 有虚函数 `area()`（默认返回 0），派生 `Circle` 半径 2；把 `Circle(2.0)` 分别放进 `std::vector<Shape>`（值容器）和 `std::vector<std::unique_ptr<Shape>>`（指针容器），打印两个容器里元素的 `name()` 和 `area()`，对比两个结果并解释切片发生在哪一步。③**迭代器/指针失效**：`std::vector<int> v = {1, 2, 3, 4};` 保存 `int* p = &v[0];`，然后 `push_back` 到 100 个元素触发扩容，再解引用 `*p`——用普通构建和 **ASan 构建**各跑一遍：普通构建打印出什么？（大概率是一个垃圾值，这就是 UB 最阴险的样子。）ASan 报告的是什么错误类型？最后写修复版：先 `reserve(100)` 再保存指针，验证指针稳定。贴出全部输出。

[参考答案 →](./02-homework-solutions.md#hw-c-2)

### C-3 {#hw-c-3}

难度 **L5** · 涉及[指针运算与数组](../ch04/02-pointer-arithmetic.md)、[循环语句](../ch02/02-loops.md)、[std::vector 快速上手](../ch11/01-vector.md)

挑战题（改编自 **LeetCode #42 Trapping Rain Water（Hard）**。本卷是入门卷，L5 口径＝「用本卷知识可解的最难问题」：这题只需要数组、循环和指针算术，双指针思路正是第 4 章教过的家底）。给定一个非负整数数组 `height`，每个柱子宽 1，求下雨后这些柱子之间能接住多少格雨水。分两步做：①先写**暴力版** `trap_bruteforce`——对每个位置 i，向左向右分别扫描出最大高度，该位置接水量为 `min(左最大, 右最大) - height[i]`（负数取 0），累加得到总量，时间复杂度 O(n²)；②再写**双指针版** `trap_twopointer`——左右两个指针相向推进，维护左右两边的最大高度，每次移动**较矮的一侧**：这一侧的水位已经被该侧最大高度决定了，所以 `height[i]` 低于该侧最大高度就接水，否则更新最大高度，时间复杂度 O(n)。用四组数据验证两个版本结果一致：`{0,1,0,2,1,0,1,3,2,1,2,1}`（= 6）、`{4,2,0,3,2,5}`（= 9）、`{2,0,2}`（= 2）、自拟一组 `{3,0,1,4}`（先手算再跑）。最后用一句话说清双指针版的关键不变量：为什么移动较矮一侧时，不需要知道另一侧的具体最大高度？

[参考答案 →](./02-homework-solutions.md#hw-c-3)

## 收尾

到这里，31 道题把卷 1 从 `sizeof` 到内存布局全部过了一遍。做完 Homework 再去[Lab](./03-lab.md)和[Project](./05-project.md)把手艺往工程里再推一步——那里没有「每题一个知识点」，只有一条流水线和一座要自己盖完的房子。
