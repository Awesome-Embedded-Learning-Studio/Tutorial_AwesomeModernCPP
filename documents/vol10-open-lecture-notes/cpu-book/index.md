---
title: "CPU 眼里的 C/C++"
description: "从一行代码钻到指令、栈帧与页表——把 C++ 的每层抽象放回 CPU 的视角里看一遍"
---

# CPU 眼里的 C/C++

咱们平时写 C++，类、引用、迭代器这些抽象替咱们把底层细节包得干干净净，咱们写起来确实省心。这个系列咱们把视角掉个头，请您跟咱们一起站到 CPU 那一侧往回看，看看一行 `i++` 落到指令层发生了什么，一次函数调用在栈上留下了什么，一个对象在内存里到底长什么样，指针值又是怎么一步步变成内存条上的真实位置。

咱们这个系列一共开了十一个主题，咱们把它们写成了三十五篇短文，您读一篇花十来分钟就够了。例子与实验全部是本站原创的，您在文中看到的每一块汇编、每一段调试器会话与每一行程序输出，都逐字来自本机的真实运行，您跟着敲就能复现，各主题第一篇的实验环境一节写着当时的编译器版本。

## 篇目

<ChapterNav variant="sub">
  <ChapterLink href="01-prefix-vs-postfix-increment/">i++ 与 ++i 生成的指令到底一样吗</ChapterLink>
  <ChapterLink href="02-function-calls-and-stack-frames/">函数的调用与返回：栈帧、call 与 ret</ChapterLink>
  <ChapterLink href="03-arguments-in-registers/">参数是怎么进寄存器的：System V AMD64 ABI 传参实录</ChapterLink>
  <ChapterLink href="04-stack-overflow-why-no-crash/">栈上数组越界为什么有时不崩</ChapterLink>
  <ChapterLink href="05-classes-in-assembly/">你的类在汇编里长什么样：this、构造函数与虚表</ChapterLink>
  <ChapterLink href="06-page-table-walk/">多级页表走一遍：虚拟地址到物理地址的翻译</ChapterLink>
  <ChapterLink href="07-what-volatile-guarantees/">volatile 到底管什么：它禁的是编译器，不是数据竞争</ChapterLink>
  <ChapterLink href="08-return-value-journey/">函数返回值的回家路：从 rax 到返回值优化</ChapterLink>
  <ChapterLink href="09-manual-backtrace-with-gdb/">gdb 手工回溯调用链：自己把 bt 爬一遍</ChapterLink>
  <ChapterLink href="10-new-in-assembly/">new 在汇编里的两步走：一次分配的完整旅程</ChapterLink>
  <ChapterLink href="11-template-instantiation/">模板实例化的对照：编译器替你写的代码长什么样</ChapterLink>
</ChapterNav>

## 鸣谢

> 非常感谢@owollz4大佬推介的书籍《CPU眼里的C/C++》，这个系列卷是对这个主题一些探索和思考而非照搬~。如果这个卷出现并不妥当的内容或者涉及侵权，速速联系 @Charliechen114514，会拔掉这个卷的内容~
