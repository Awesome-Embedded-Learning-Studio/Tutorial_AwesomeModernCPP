# C 语言系统教程

如果你是因为一些原因打算学习或重修 C 语言，发现上一级目录的 C 语言速通教程节奏太紧，可以考虑这里的教程。它比速通教程更加详细，循序渐进，同样可以根据自己的喜好挑感兴趣的方向学习。

## 基础篇

| # | 文章 | 简介 |
|---|------|------|
| 01 | [程序结构与编译基础](01-program-structure-and-compilation.md) | C 程序的基本结构、编译四阶段流程、头文件机制和基本 I/O |
| 02A | [数据类型基础：整数与内存](02A-data-types-basics.md) | 整型家族、有符号与无符号、固定宽度类型和 sizeof |
| 02B | [浮点、字符、const 与类型转换](02B-float-char-const-cast.md) | 浮点精度、字符编码、const 限定符和隐式类型转换 |
| 03A | [运算符基础：让数据动起来](03A-operators-basics.md) | 算术、关系、逻辑运算符，短路求值和赋值运算符 |
| 03B | [位运算与求值顺序](03B-bitwise-and-evaluation.md) | 位运算操作、移位注意事项、优先级陷阱与序列点 |
| 04 | [控制流：让程序学会选择和重复](04-control-flow.md) | 条件分支、循环、switch 穿透与状态机模式 |
| 05 | [函数基础与参数传递](05-function-basics.md) | 函数声明/定义/调用、值传递、指针参数与递归 |
| 06 | [作用域与存储类别](06-scope-and-storage.md) | 作用域规则、存储类别、链接性和 static 的三种用法 |
| 07A | [指针入门：地址的世界](07A-pointer-essentials.md) | 内存模型、取地址与解引用、指针运算和距离计算 |
| 07B | [指针与数组、const 和空指针](07B-pointers-arrays-const.md) | 数组退化为指针、const 与指针组合、NULL 和野指针 |
| 08A | [多级指针与声明读法](08A-multi-level-pointers.md) | 多级指针内存模型、指针数组 vs 数组指针、cdecl 读法 |
| 08B | [restrict、不完整类型与结构体指针](08B-restrict-incomplete-types.md) | restrict 优化、前向声明、opaque pointer 模式 |
| 09 | [函数指针与回调模式](09-function-pointers-and-callbacks.md) | 函数指针声明与使用、回调模式与事件驱动编程 |
| 10 | [数组深入](10-arrays-deep-dive.md) | 内存布局、多维数组、变长数组及其与指针的关系 |
| 11 | [C 字符串与缓冲区安全](11-c-strings-and-buffer-safety.md) | `\0` 终止模型、string.h 核心函数、缓冲区溢出防范 |
| 12 | [结构体与内存对齐](12-struct-and-memory-alignment.md) | 结构体定义、对齐填充规则、柔性数组成员 |
| 13 | [联合体、枚举、位域与 typedef](13-union-enum-bitfield-typedef.md) | 类型双关、硬件寄存器映射，对比 C++ 类型安全方案 |
| 14 | [动态内存管理](14-dynamic-memory.md) | malloc/calloc/realloc/free、常见内存错误及调试 |
| 15 | [预处理器与多文件工程](15-preprocessor-and-multifile.md) | 宏、条件编译、头文件防护、模块化多文件工程 |
| 16 | [文件 I/O 与标准库概览](16-file-io-and-stdlib.md) | 文件读写、格式化 I/O、命令行参数处理 |

## 进阶专题

进阶专题位于 [advanced_feature/](advanced_feature/) 子目录，涵盖更深入的主题：

| # | 文章 | 简介 |
|---|------|------|
| 01 | [ARM 架构与体系结构基础](advanced_feature/01-arm-architecture-fundamentals.md) | ARM Cortex-M 指令集、寄存器、异常向量表与处理器模式 |
| 02 | [Cache 机制与内存层次](advanced_feature/02-cache-and-memory-hierarchy.md) | 缓存行、映射策略、MESI 协议与缓存友好编程 |
| 03 | [C 语言陷阱与常见错误](advanced_feature/03-c-traps-and-pitfalls.md) | 语法与语义陷阱，编译器行为与标准规范分析 |
| 04 | [用 C 实现面向对象编程](advanced_feature/04-oop-in-c.md) | 结构体 + 函数指针模拟类、封装、继承与多态 |
| 05 | [手搓动态数组](advanced_feature/05-handmade-dynamic-array.md) | 类型安全动态数组库，内存扩缩容与 API 设计 |
| 06 | [手搓单链表](advanced_feature/06-handmade-linked-list.md) | 插入、删除、查找算法与哨兵节点技巧 |
| 07 | [嵌入式 C 编程模式](advanced_feature/07-embedded-c-patterns.md) | 寄存器访问、volatile、中断安全与外设抽象层 |
| 08 | [构建可复用的 C 代码](advanced_feature/08-reusable-c-code.md) | 模块化设计、不透明指针、平台抽象层 |
