---
title: "卷 2 课后练习（Homework）"
description: "现代特性（C++11/14/17）卷的课后练习：12 章每章 2 题（基础+进阶），另加 2 道跨章综合与 1 道 L5 挑战（SBO 类型擦除容器，本卷知识可解的最难问题）。难度覆盖 L1~L5，题目都做变式处理，参考答案独立成文件、逐步解答附知识点链接。"
chapter: 2
order: 1
tags: [host, intermediate, cpp-modern, 移动语义, 类型安全]
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 17]
reading_time_minutes: 25
prerequisites:
  - "卷 2 全部章节（第 0~11 章）"
related:
  - "卷 2 Lab：零拷贝配置读取器"
  - "卷 2 Project：logscan 日志巡检器"
---

# 卷 2 课后练习（Homework）

## 引言

这里的题按章组织，每章两道（基础 + 进阶），最后是两道跨章综合和一道 L5 挑战。每题标注难度档位（L1~L5，见[练习总览](./index.md)）和涉及章节；题目都是「变式」——换场景、换数据、换推理方向，照抄教材例题抄不出答案。每道题都要真编译真跑，把输出贴下来才算完。

答案在独立的[参考答案](./02-homework-solutions.md)文件里，按题号对应，每步解答带知识点链接。建议一章做完再看答案。所有代码用 `g++ -std=c++17 -Wall -Wextra` 起步（个别题目要求 ASan/UBSan 或其他旗标，题面会写明），本卷题目都在卷 2 的知识范围内——`std::expected` 是 C++23 的，题面要求时就用自制简化版。

## 2.1 移动语义与右值引用

### 2.1-A {#hw-2-1-a}

难度 **L1** · 涉及[右值引用：从拷贝到移动](../ch00-move-semantics/01-rvalue-reference.md)

用 `decltype((expr))` 探针写一个「值类别体检表」：对下面这 11 个表达式逐一打印它们属于 lvalue、xvalue 还是 prvalue，再配一组 `static_assert` 把关键结论钉死：普通变量 `x`、左值引用 `lref`、命名右值引用 `rref`、字面量 42、`x + 1`、`std::move(x)`、`++x`、`x++`、解引用指针 `*p`、`"hi"[0]`、指针变量 `p`。哪个结果最反直觉？一句话解释它。

[参考答案 →](./02-homework-solutions.md#hw-2-1-a)

### 2.1-B {#hw-2-1-b}

难度 **L3** · 涉及[移动构造与移动赋值](../ch00-move-semantics/02-move-semantics.md)

写三个类型做「Rule of Five 体检」：`A` 五个特殊成员全 `= default`、成员全是 `std::string`/`std::vector`；`B` 只自定义析构（管一块 `new[]` 出来的内存）；`C` 自定义析构加拷贝构造、不声明移动。对每个类型打印 `is_move_constructible`、`is_nothrow_move_constructible`、`is_trivially_move_constructible`、`is_copy_constructible` 四个 trait。先预测 B 和 C 的 `is_move_constructible` 为什么是 1，再预测它们的 `is_nothrow_move_constructible` 是几；然后对 B 做一次实测：`B b = std::move(a);` 用 ASan 跑，贴出报告，说明「看起来能移动」背后发生了什么。

[参考答案 →](./02-homework-solutions.md#hw-2-1-b)

## 2.2 智能指针与 RAII

### 2.2-A {#hw-2-2-a}

难度 **L2** · 涉及[unique_ptr 详解：独占所有权的零开销智能指针](../ch01-smart-pointers/02-unique-ptr.md)、[自定义删除器与侵入式引用计数](../ch01-smart-pointers/05-custom-deleter.md)

用三种删除器形态管理同一个 `FILE*`（写入 `/tmp` 下的一个文件）：函数指针、无捕获 lambda、空函数对象。打印三种 `unique_ptr<FILE, ...>` 的 `sizeof` 并解释差异（哪两种是 8、哪种是 16？为什么）。再用函数对象版本实际写一次文件，用静态计数器证明析构时删除器恰好被调用一次。

[参考答案 →](./02-homework-solutions.md#hw-2-2-a)

### 2.2-B {#hw-2-2-b}

难度 **L3** · 涉及[shared_ptr 详解：共享所有权与引用计数](../ch01-smart-pointers/03-shared-ptr.md)、[weak_ptr 与循环引用：打破所有权的死锁](../ch01-smart-pointers/04-weak-ptr.md)

做一个「双向链表」小实验：`Node` 持有 `next` 和 `prev` 两个 `shared_ptr`，两个节点互相指一下再离开作用域。用 ASan 构建运行，贴出泄漏报告（析构函数有没有被调用？）；然后把 `prev` 改成 `weak_ptr` 再跑一遍，贴出修复后的输出。顺带解释：为什么 LeakSanitizer 跑起来时你的构造/析构日志可能「消失」了一部分——程序里该怎么防这个坑。

[参考答案 →](./02-homework-solutions.md#hw-2-2-b)

## 2.3 constexpr 与编译期计算

### 2.3-A {#hw-2-3-a}

难度 **L1** · 涉及[constexpr 基础：编译期求值的艺术](../ch02-constexpr/01-constexpr-basics.md)

写 C++14 风格的 `constexpr` 阶乘与斐波那契（循环/if-else 都允许），配四组 `static_assert`；再用 `factorial(4) + 1` 声明一个全局数组的大小并打印元素个数。然后写一对对比：`constexpr int kGood = factorial(6);` 和 `const int kRuntime = runtime_seed();`（`runtime_seed` 是普通函数）——注释掉会编译失败的那一行，解释 `const` 和 `constexpr` 到底差在哪。

[参考答案 →](./02-homework-solutions.md#hw-2-3-a)

### 2.3-B {#hw-2-3-b}

难度 **L3** · 涉及[编译期计算实战：从查表到编译期字符串](../ch02-constexpr/04-compile-time-practice.md)

把教材的 CRC-32 查表换成 **CRC-16/CCITT-FALSE**（多项式 `0x1021`、初值 `0xFFFF`）：用 `constexpr` 在编译期生成 256 项查表，再用查表函数对 `"123456789"` 做 `static_assert`（标准校验值是 `0x29B1`），最后运行时对 `"hello"` 算一次并打印十六进制结果，顺带打印表的前几项。

[参考答案 →](./02-homework-solutions.md#hw-2-3-b)

## 2.4 Lambda 与函数式编程

### 2.4-A {#hw-2-4-a}

难度 **L2** · 涉及[Lambda 基础：匿名函数的优雅表达](../ch03-lambda/01-lambda-basics.md)、[Lambda 捕获机制深入](../ch03-lambda/02-lambda-capture.md)

写四个小实验并**先预测输出、再真跑对照**：①值捕获的快照语义（捕获 `threshold = 100`，lambda 创建后把它改成 200，`is_high(150)` 是几？）；②`mutable` 计数器连调三次后外部 `counter` 是几；③引用捕获累加器连加 10/20/30 后 `sum` 是几；④初始化捕获把 `std::unique_ptr<int>` 移进闭包后，外部指针还有没有效。四组预测全对才算过关。

[参考答案 →](./02-homework-solutions.md#hw-2-4-a)

### 2.4-B {#hw-2-4-b}

难度 **L3** · 涉及[泛型 Lambda 与模板 Lambda](../ch03-lambda/03-generic-lambda.md)

lambda 没有名字，怎么递归？用 Y 组合子（第一个参数传「自己」的惯用法）写两个泛型 lambda 递归：一个算阶乘（`long long`，验证 `factorial(12)`），一个求 `std::vector<int>` 的最大元素（`auto&& self` 参数 + 索引下探，验证 `{3,8,1,9,4,7}` 的最大值）。说清这个 Y 组合子的 `operator()` 为什么能零开销内联，而 `std::function` 版本的递归慢在哪。

[参考答案 →](./02-homework-solutions.md#hw-2-4-b)

## 2.5 类型安全

### 2.5-A {#hw-2-5-a}

难度 **L1** · 涉及[enum class 与强类型枚举](../ch04-type-safety/01-enum-class.md)

验证 `enum class` 的三大改进：①声明底层类型 `uint8_t` 的 `Color`，打印 `sizeof` 并用 `static_assert` 钉死；②`static_cast<int>(Color::Green)` 是多少，隐式转换的两行错误代码以注释形式留在程序里；③写一个覆盖四种状态的 `to_string(NetworkState)` switch——**不写 default**，先跑通；再复制一份故意漏掉一个分支的版本，编译并贴出 `-Wswitch` 的警告，说明「不写 default」为什么是防漏分支的正确姿势。

[参考答案 →](./02-homework-solutions.md#hw-2-5-a)

### 2.5-B {#hw-2-5-b}

难度 **L3** · 涉及[std::variant：类型安全的联合体](../ch04-type-safety/03-variant.md)、[unique_ptr 详解：独占所有权的零开销智能指针](../ch01-smart-pointers/02-unique-ptr.md)

教材给了一个 AST 节点定义但没给求值器。补全它：`Expr = variant<NumberLiteral, unique_ptr<BinaryExpr>>`（递归结构必须走指针，为什么？），用 `Overloaded` 访问者写递归求值函数 `eval`，对表达式 $\frac{(2 + 3) \times (10 - 4)}{3}$ 求值并打印结果（应是多少？先手算）。

[参考答案 →](./02-homework-solutions.md#hw-2-5-b)

## 2.6 结构化绑定与初始化

### 2.6-A {#hw-2-6-a}

难度 **L1** · 涉及[结构化绑定：一行解包多个值](../ch05-structured-bindings/01-structured-bindings.md)

四个解包对象各来一遍：map 迭代（键是 `uint8_t`——打印时为什么前面要加个 `+`？）、函数返回的 tuple、原生数组、结构体。最后做「auto vs auto&」语义对比：`auto [id, val, ts] = reading;` 之后改 `val`，原对象变不变？换成 `auto& [...]` 再改一次呢？把两次打印的结果都贴出来。

[参考答案 →](./02-homework-solutions.md#hw-2-6-a)

### 2.6-B {#hw-2-6-b}

难度 **L2** · 涉及[if/switch 初始化器：缩小变量作用域](../ch05-structured-bindings/02-init-statements.md)

三件套：①map `insert` + 结构化绑定塞进 if 初始化器，验证插入已存在 key 时 `it` 在 else 分支照样可见、新值不覆盖旧值；②用一个会打印「锁获取/锁释放」的 RAII 追踪类当 if 初始化器，验证 else 分支执行时锁还没释放；③switch 初始化器 + 编译期字符串哈希分派（`hash_string` 是 `constexpr`，case 标签用 `hash_string("start")` 这种常量），验证三个输入和一个未知输入的分派结果。

[参考答案 →](./02-homework-solutions.md#hw-2-6-b)

## 2.7 auto 与 decltype

### 2.7-A {#hw-2-7-a}

难度 **L2** · 涉及[auto 推导深入：不只是偷懒](../ch06-auto-decltype/01-auto-deep-dive.md)

「auto 推导体检」：一组 `static_assert` 钉死这些推导结果——`auto` 丢顶层 const、丢引用、保底层 const；`auto&` 保留 const 引用；`auto&&` 对左值得 `int&`、对右值得 `int&&`；`auto x = {1,2,3}` 是 `initializer_list` 不是 vector；`vector<bool>` 的 `operator[]` 用 `auto` 拿到的是代理不是 `bool&`；返回引用的函数用 `auto` 接收是拷贝。全部通过后打印 `initializer_list` 的大小和 `vector<bool>[0]` 的值。

[参考答案 →](./02-homework-solutions.md#hw-2-7-a)

### 2.7-B {#hw-2-7-b}

难度 **L3** · 涉及[decltype 与返回类型推导](../ch06-auto-decltype/02-decltype.md)

四个维度：①`decltype(x)` vs `decltype((x))`、`decltype(++x)` vs `decltype(x++)`、`decltype(x = 10)` 各是什么类型（`static_assert` 钉死）；②用 `decltype(auto)` 给一个 `Container` 写 `operator[]`，验证 `c[0] = 99;` 能编译——换成 `auto` 会怎样？③写一个 `decltype(auto)` 的完美转发返回包装器，把「返回 `int&` 并修改调用者变量」的 lambda 传进去，验证引用语义完整穿透；④一句话说清 `return (x);` 的悬垂陷阱什么时候会咬人。

[参考答案 →](./02-homework-solutions.md#hw-2-7-b)

## 2.8 属性系统

### 2.8-A {#hw-2-8-a}

难度 **L1** · 涉及[标准属性详解：让编译器成为你的代码审查员](../ch07-attributes/01-standard-attributes.md)

写一个「故意惹编译器」的小程序：一个返回 `int` 的 `[[nodiscard]]` 函数被调用却丢掉返回值；一个 `[[deprecated("Use new_handler() instead")]]` 的旧函数被调用；一个 `[[maybe_unused]]` 参数；一个 case 里 `[[fallthrough]]` 有意贯穿。用 `-Wall` 编译，把全部警告原样贴下来，并逐条说清哪些警告来自哪个属性、`[[fallthrough]]` 为什么一条警告都不产生。

[参考答案 →](./02-homework-solutions.md#hw-2-8-a)

### 2.8-B {#hw-2-8-b}

难度 **L2** · 涉及[标准属性详解：让编译器成为你的代码审查员](../ch07-attributes/01-standard-attributes.md)、[enum class 与强类型枚举](../ch04-type-safety/01-enum-class.md)

做一个「传感器驱动迁移」小模块：`enum class` 错误码（带 `[[nodiscard]]`，注意这个属性在 GCC 16 下该放在 `enum class` 关键字的**哪一侧**——放错会被 `-Wattributes` 点名，先试错再修好）、一个 `[[deprecated]]` 旧接口和一个返回错误码的新接口、一个用 `[[fallthrough]]` 共享初始化逻辑的四态状态机（Idle/Starting/Running/Paused+Error，不写 default）。最终 `-Wall -Wextra` 编译**零警告**，运行打印传感器读数和两次状态迁移。

[参考答案 →](./02-homework-solutions.md#hw-2-8-b)

## 2.9 string_view 深入

### 2.9-A {#hw-2-9-a}

难度 **L2** · 涉及[string_view 内部原理：非拥有字符串视图](../ch08-string-view/01-string-view-internals.md)

把教材的 `key=value` 解析换成 **HTTP 头解析**：输入 `"Host: example.com, Accept: */*, Content-Length: 42"`，写 `parse_header`（冒号分隔、两侧 trim 空白、`optional` 返回），主循环用 `remove_prefix` 逐段消费。输出三段头。说清整个解析过程中哪一步发生过堆分配、`std::string` 版同样的逻辑会多出多少分配。

[参考答案 →](./02-homework-solutions.md#hw-2-9-a)

### 2.9-B {#hw-2-9-b}

难度 **L4** · 涉及[string_view 陷阱与最佳实践](../ch08-string-view/03-string-view-pitfalls.md)

「悬垂 view 侦探」：写两个独立的陷阱程序各埋一颗雷——①函数返回指向局部 `std::string` 的 view，main 里解引用它；②把一个**临时** `std::string("  hello")` 传给返回 view 的 `trim`，main 里解引用返回值。分别用 ASan（`-fsanitize=address,undefined -fsanitize-address-use-after-scope`）构建运行，贴出两份报告，指出两份报告的类型为什么不一样（一个 heap、一个 stack）。最后写修复版（返回 `std::string` 让调用端持view；或者用活得够久的具名 `std::string`），ASan 下跑通零报告。

[参考答案 →](./02-homework-solutions.md#hw-2-9-b)

## 2.10 文件系统库

### 2.10-A {#hw-2-10-a}

难度 **L2** · 涉及[path 操作：跨平台路径处理](../ch09-filesystem/01-filesystem-path.md)

对三个路径 `/home/user/docs/report.tar.gz`、`config.ini`、`/tmp/archive.tar.gz` 打印 `root_path/parent/filename/stem/extension` 全套分解；然后做三组修改实验：`replace_extension(".txt")`（**特别注意**：调用后原 `path` 对象变没变？贴真实输出）、`f += ".txt"` vs `f2 /= ".txt"` 的差异、`base / "/tmp/x"` 绝对右操作数会怎样。把每个结果和教材的说法对照一遍。

[参考答案 →](./02-homework-solutions.md#hw-2-10-a)

### 2.10-B {#hw-2-10-b}

难度 **L3** · 涉及[目录遍历与搜索](../ch09-filesystem/03-directory-iteration.md)、[文件与目录操作](../ch09-filesystem/02-filesystem-ops.md)

写一个「目录清点」工具：在 `/tmp` 下自建一棵测试目录树（两个扩展名 .txt 的文件加一个 .log、一个 .bin，塞进子目录一个），用 `recursive_directory_iterator` + `skip_permission_denied` 遍历，按扩展名分组统计「文件数 + 总字节数」，并找出最大的文件。输出分组统计和最大文件（名字 + 字节数）。

[参考答案 →](./02-homework-solutions.md#hw-2-10-b)

## 2.11 错误处理的现代方式

### 2.11-A {#hw-2-11-a}

难度 **L2** · 涉及[optional 用于错误处理](../ch10-error-handling/02-optional-error.md)、[std::optional：优雅表达「可能没有值」](../ch04-type-safety/04-optional.md)

用 `std::from_chars` 写 `parse_int`/`parse_double`（`optional` 返回，非法输入得 `nullopt`），验证 `"42"`、`"42a"`、`"3.14"`、`"x"` 四个输入；再写一个 map 查找的 `optional` 封装，验证命中与未命中；最后用 `value_or` 给一个空配置项补默认值 `"INFO"`。全部 `value_or` 收尾，输出贴全。

[参考答案 →](./02-homework-solutions.md#hw-2-11-a)

### 2.11-B {#hw-2-11-b}

难度 **L3** · 涉及[`std::expected<T, E>`：类型安全的错误传播](../ch10-error-handling/03-expected-error.md)、[错误处理模式总结：选择指南与最佳实践](../ch10-error-handling/04-error-patterns.md)

教材给了 C++17 简化版 `expected`，但那份实现有个**编译坑**：匿名 union 里放着 `std::string` 这类非平凡成员时，union 的默认构造/析构是被删除的，直接编译不过——先复现这个报错，再修好它（给 union 起名并补上空的构造/析构）。然后完成「地址解析链」：`validate_input` → `split_address`（拆 host:port、`from_chars` 解析端口、范围检查）→ `transform` 拼回字符串，用 `and_then` 串起来，对 `"192.168.1.1:8080"`、`"localhost"`、`":9090"`、`"host:99999"`、`""` 五个输入跑一遍，错误分支各贴一条。

[参考答案 →](./02-homework-solutions.md#hw-2-11-b)

## 2.12 用户自定义字面量

### 2.12-A {#hw-2-12-a}

难度 **L2** · 涉及[用户自定义字面量基础](../ch11-user-defined-literals/01-udl-basics.md)

自定义三组字面量：`_ms`（毫秒）、`_KiB`（字节，`4_KiB` = 4096）、`_kHz`（浮点版，`1.5_kHz` = 1500 赫兹），全部 `constexpr` 并各配一条 `static_assert`。再演示标准库字面量：`1s + 500ms` 是多少毫秒？`"hello"sv` 的长度是多少？顺带说清：为什么你的后缀必须以下划线开头。

[参考答案 →](./02-homework-solutions.md#hw-2-12-a)

### 2.12-B {#hw-2-12-b}

难度 **L3** · 涉及[UDL 实战：类型安全的单位系统](../ch11-user-defined-literals/02-udl-practice.md)

完成教材的「长度单位系统」练习：`Quantity<T, UnitTag>` 模板（加/减/标量乘/比较）+ `_m`/`_km`/`_s`/`_h` 字面量 + `Length / Duration -> Speed` 与 `Speed * Duration -> Length` 两个跨单位运算。四条 `static_assert`：`1.0_km + 500.0_m == 1500`、`36.0_km / 1.0_h == 10`（36 km/h = 10 m/s）、`v1 * 90.0_s == 900`、`2 * 100.0_m == 200`（注意 `2` 是 `int` 而量是 `long double`——模板推导会撞车，教材给过解法，用它）。「长度 + 时间」和「长度 × 时间」的编译错误以注释形式留在程序里。

[参考答案 →](./02-homework-solutions.md#hw-2-12-b)

## 2.C 跨章综合与挑战

### 2.C-1 {#hw-2-c-1}

难度 **L3** · 涉及[string_view 内部原理：非拥有字符串视图](../ch08-string-view/01-string-view-internals.md)、[std::variant：类型安全的联合体](../ch04-type-safety/03-variant.md)、[结构化绑定：一行解包多个值](../ch05-structured-bindings/01-structured-bindings.md)

**零拷贝配置解析器**：输入一行 `"host=192.168.1.1;port=8080;debug=true;pi=3.14;name=alpha"`，用 `string_view` 零拷贝拆出五个键值对；值按「bool → int → double → 字符串」的优先级解析成 `variant<int, double, string_view, bool>`；用 `Overloaded` 访问者打印「键 = 值 (类型)」。注意一个坑：lambda 里想捕获结构化绑定变量 `key`——这在 C++17 不合法（C++20 才允许）：显式捕获时 clang 和 gcc 都会警告「这是 C++20 扩展」，换**隐式捕获** gcc 16 在 struct 分解下仍警告、只在 tuple-like 分解（pair/tuple）下才静默——「不合法」与「编译器拦不拦」是两回事，拦不拦还随分解形状变（旧版教材在这个点上的说法就栽过跟头，现行版已修订——所以别只信文档，要信编译器实测），用初始化捕获把它修成干净的 C++17。

[参考答案 →](./02-homework-solutions.md#hw-2-c-1)

### 2.C-2 {#hw-2-c-2}

难度 **L4** · 涉及[完美转发：保持值类别的精确传递](../ch00-move-semantics/04-perfect-forwarding.md)、[右值引用：从拷贝到移动](../ch00-move-semantics/01-rvalue-reference.md)、[unique_ptr 详解：独占所有权的零开销智能指针](../ch01-smart-pointers/02-unique-ptr.md)

**完美转发工厂与移动追踪**（Effective Modern C++ 风格机制分析）：自己写 `make_object<T>(Args&&...)`（仿 `make_unique` 的转发工厂），目标类型 `Tracked` 的构造/拷贝/移动/析构都打日志并计数。四个实验各跑一遍并贴真实追踪输出：A 传左值 `std::string`；B 传 `std::move(name)`；C 传字符串字面量；D 连续三次 `vector<unique_ptr<Tracked>>::push_back(make_object<Tracked>(...))`。每个实验后报出 `ctor/copy/move` 计数，并解释：为什么 C 是零拷贝？为什么 D 里三个对象只构造、从不拷贝/移动（两个机制各自是谁的功劳）？

[参考答案 →](./02-homework-solutions.md#hw-2-c-2)

### 2.C-3 {#hw-2-c-3}

难度 **L5** · 涉及[std::any 与类型擦除](../ch04-type-safety/05-any.md)、[std::function、std::invoke 与可调用对象](../ch03-lambda/04-std-function.md)、[移动构造与移动赋值](../ch00-move-semantics/02-move-semantics.md)、[RAII 深入理解：资源管理的基石](../ch01-smart-pointers/01-raii-deep-dive.md)

**MiniAny：带 SBO 的类型擦除容器**。教材 ch04 第七步的手写 `MiniAny` 每次构造都走堆；本挑战要求升级成生产级形态：任意**可拷贝构造**类型；对象 ≤ 32 字节时存进容器自带的栈上缓冲区（SBO），超过才堆分配；用**函数指针表**替代虚函数（`copy/move/destroy/type/size` 五项）；类型安全的 `mini_any_cast<T>`（类型不符抛 `BadMiniAnyCast`）；正确的拷贝/移动/析构，移动语义下源对象被清空；用「属性字典」验证 int/double/string 混存。最后用 ASan/UBSan 全绿收尾。

两道暗雷，答出来才算真的做对：①把内容「字节级交换」进另一个 MiniAny（比如把两块 SBO 缓冲按字节对调）对短 `std::string` 为什么会炸——SSO 的内部指针指向自己栈上的本地缓冲，字节对调后析构试图 free 栈上地址（报 invalid free）；用 `memcpy` 复制**堆上**字符串指针则是 double free。正确做法是走 vtable 的**类型化**搬运；②`mini_any_cast<int&>(a)` 和 `mini_any_cast<int>(std::move(a))` 语义差在哪（引用就地改 vs 按值搬出）。本题口径：难度按「用本卷知识可解的最难问题」标定 L5，题源为 cppreference 的 any 教学实现与教材 ch04-any/ch03-std_function 两节的机制组合、按工程挑战强化。

[参考答案 →](./02-homework-solutions.md#hw-2-c-3)
