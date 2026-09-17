# C 语言系统教程

PS: 这部分教程不是面向0基础的朋友的，本教程的原型是笔者曾今搞嵌入式的C语言笔记，当时写笔记的时候就已经掌握了C语言，所以如果存在C语言的学习需求，左转到这个仓库：

> [Github C语言的旅程](https://github.com/Awesome-Embedded-Learning-Studio/C-Journey)
> [网站 C Journey](https://awesome-embedded-learning-studio.github.io/C-Journey/)

这里的C语言教程更偏向于曾学习过C但是忘记C长啥样的朋友看的。

## 基础篇

<ChapterNav variant="sub">
  <ChapterLink num="01" href="01-program-structure-and-compilation" desc="C 程序的基本结构、编译四阶段流程、头文件机制和基本 I/O">程序结构与编译基础</ChapterLink>
  <ChapterLink num="02A" href="02A-data-types-basics" desc="整型家族、有符号与无符号、固定宽度类型和 sizeof">数据类型基础：整数与内存</ChapterLink>
  <ChapterLink num="02B" href="02B-float-char-const-cast" desc="浮点精度、字符编码、const 限定符和隐式类型转换">浮点、字符、const 与类型转换</ChapterLink>
  <ChapterLink num="03A" href="03A-operators-basics" desc="算术、关系、逻辑运算符，短路求值和赋值运算符">运算符基础：让数据动起来</ChapterLink>
  <ChapterLink num="03B" href="03B-bitwise-and-evaluation" desc="位运算操作、移位注意事项、优先级陷阱与序列点">位运算与求值顺序</ChapterLink>
  <ChapterLink num="04" href="04-control-flow" desc="条件分支、循环、switch 穿透与状态机模式">控制流：让程序学会选择和重复</ChapterLink>
  <ChapterLink num="05" href="05-function-basics" desc="函数声明/定义/调用、值传递、指针参数与递归">函数基础与参数传递</ChapterLink>
  <ChapterLink num="06" href="06-scope-and-storage" desc="作用域规则、存储类别、链接性和 static 的三种用法">作用域与存储类别</ChapterLink>
  <ChapterLink num="07A" href="07A-pointer-essentials" desc="内存模型、取地址与解引用、指针运算和距离计算">指针入门：地址的世界</ChapterLink>
  <ChapterLink num="07B" href="07B-pointers-arrays-const" desc="数组退化为指针、const 与指针组合、NULL 和野指针">指针与数组、const 和空指针</ChapterLink>
  <ChapterLink num="08A" href="08A-multi-level-pointers" desc="多级指针内存模型、指针数组 vs 数组指针、cdecl 读法">多级指针与声明读法</ChapterLink>
  <ChapterLink num="08B" href="08B-restrict-incomplete-types" desc="restrict 优化、前向声明、opaque pointer 模式">restrict、不完整类型与结构体指针</ChapterLink>
  <ChapterLink num="09" href="09-function-pointers-and-callbacks" desc="函数指针声明与使用、回调模式与事件驱动编程">函数指针与回调模式</ChapterLink>
  <ChapterLink num="10" href="10-arrays-deep-dive" desc="内存布局、多维数组、变长数组及其与指针的关系">数组深入</ChapterLink>
  <ChapterLink num="11" href="11-c-strings-and-buffer-safety" desc="\0 终止模型、string.h 核心函数、缓冲区溢出防范">C 字符串与缓冲区安全</ChapterLink>
  <ChapterLink num="12" href="12-struct-and-memory-alignment" desc="结构体定义、对齐填充规则、柔性数组成员">结构体与内存对齐</ChapterLink>
  <ChapterLink num="13" href="13-union-enum-bitfield-typedef" desc="类型双关、硬件寄存器映射，对比 C++ 类型安全方案">联合体、枚举、位域与 typedef</ChapterLink>
  <ChapterLink num="14" href="14-dynamic-memory" desc="malloc/calloc/realloc/free、常见内存错误及调试">动态内存管理</ChapterLink>
  <ChapterLink num="15" href="15-preprocessor-and-multifile" desc="宏、条件编译、头文件防护、模块化多文件工程">预处理器与多文件工程</ChapterLink>
  <ChapterLink num="16" href="16-file-io-and-stdlib" desc="文件读写、格式化 I/O、命令行参数处理">文件 I/O 与标准库概览</ChapterLink>
</ChapterNav>

## 进阶专题

进阶专题位于 [advanced_feature/](advanced_feature/) 子目录，涵盖更深入的主题：

<ChapterNav variant="sub">
  <ChapterLink num="01" href="advanced_feature/01-arm-architecture-fundamentals" desc="ARM Cortex-M 指令集、寄存器、异常向量表与处理器模式">ARM 架构与体系结构基础</ChapterLink>
  <ChapterLink num="02" href="advanced_feature/02-cache-and-memory-hierarchy" desc="缓存行、映射策略、MESI 协议与缓存友好编程">Cache 机制与内存层次</ChapterLink>
  <ChapterLink num="03" href="advanced_feature/03-c-traps-and-pitfalls" desc="语法与语义陷阱，编译器行为与标准规范分析">C 语言陷阱与常见错误</ChapterLink>
  <ChapterLink num="04" href="advanced_feature/04-oop-in-c" desc="结构体 + 函数指针模拟类、封装、继承与多态">用 C 实现面向对象编程</ChapterLink>
  <ChapterLink num="05" href="advanced_feature/05-handmade-dynamic-array" desc="类型安全动态数组库，内存扩缩容与 API 设计">手搓动态数组</ChapterLink>
  <ChapterLink num="06" href="advanced_feature/06-handmade-linked-list" desc="插入、删除、查找算法与哨兵节点技巧">手搓单链表</ChapterLink>
  <ChapterLink num="07" href="advanced_feature/07-embedded-c-patterns" desc="寄存器访问、volatile、中断安全与外设抽象层">嵌入式 C 编程模式</ChapterLink>
  <ChapterLink num="08" href="advanced_feature/08-reusable-c-code" desc="模块化设计、不透明指针、平台抽象层">构建可复用的 C 代码</ChapterLink>
</ChapterNav>
