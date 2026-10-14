---
title: "volatile 的标准边界：弃用清单、设备寄存器与判断法"
chapter: 7
order: 3
tags: [host, cpp-modern, intermediate]
difficulty: intermediate
platform: host
cpp_standard: [11, 20]
description: "把 cppreference 的原文与 P1152R4 的弃用清单逐句对回前面的实验，再到 mmio 设备寄存器看 volatile 名正言顺的用场，收在改值的对面是谁这条判断法上"
reading_time_minutes: 10
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
---

# volatile 的标准边界：弃用清单、设备寄存器与判断法

上一篇的收尾停在 lock 前缀上：volatile 的 addl 与 atomic 的 lock addl，指令的本体一模一样，分界落在了机器码里。这一篇咱们把余下的事做完：请 cppreference 与提案的原文出来对一遍，看 C++20 往回收了哪些写法，再到设备寄存器那里看 volatile 名正言顺的用场，最后把三篇的内容归成一条判断法。咱们用的环境与[第一篇](01-volatile-bans-in-one-thread.md)相同，您要复跑就照它来。

## 标准原文怎么说

咱们把实验做完了，标准的原文也该请出来对一遍了。[cppreference 的 cv 限定符页](https://en.cppreference.com/w/cpp/language/cv)（2026-10-12 检索）主文里有一段，把 volatile 的语义和边界一次写全：

```text
Every access (read or write operation, member function call, etc.) made
through a glvalue expression of volatile-qualified type is treated as a
visible side-effect for the purposes of optimization (that is, within a
single thread of execution, volatile accesses cannot be optimized out or
reordered with another visible side effect that is sequenced-before or
sequenced-after the volatile access.
```

咱们把它翻过来：通过 volatile 类型的表达式做的每一次访问（读、写、成员调用都算），都会被当作可见的副作用对待。这一切的适用范围是单个执行线程之内，volatile 的访问既不可被优化消除，也不许与排序前后其他可见的副作用重排。这句话咱们这一主题都在兑现：第一篇里循环实验的装载不合并、死读死写实验的访问不消除、顺序实验的次序保持，全是它的现场。您再留意句中那个限定的短语，`within a single thread of execution`，说的就是单线程之内的边界。禁令的作用域写得清清楚楚，线程之间的任何保证，这句话里是一个字都没写的。紧跟着的适用范围句更直白：

```text
This makes volatile objects suitable for communication with a signal
handler, but not with another thread of execution, see std::memory_order).
```

这句话咱们翻过来是：volatile 的对象适合拿来与信号处理器通信，不适合的对象是另一个执行线程。信号处理器是操作系统在信号到来时异步调用的函数，它与主流程是并发着跑的，这是标准认可的合法用场。而另一个执行线程的场合，标准明说了不适合，咱们上一篇的竞争实验就是后半句的现场版。同页还有一句关于方向的反面提醒：

```text
Any attempt to access a volatile object through a glvalue of non-volatile
type (e.g. through a reference or pointer to non-volatile type) results in
undefined behavior.
```

拿非 volatile 类型的引用或指针去访问一个 volatile 对象，换来的结果是未定义行为。volatile 认的是表达式的类型，而不是对象实际上危不危险，您把 volatile 对象的地址转型成普通指针再用，禁令就整个失了灵，而失灵这件事本身就是 UB。

## C++20 在往回收的边界

标准对 volatile 的另一个动作是往回收。C++20 接受了一份叫 P1152R4 的提案，标题写的就是 Deprecating volatile，检索的日期是 2026-10-12，原文咱们也核过：[提案原文](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2019/p1152r4.html)。摘要的两句原文：

```text
We propose deprecating most of `volatile`.
The proposed deprecation preserves the useful parts of `volatile`, and
removes the dubious / already broken ones.
```

咱们直译：提案建议弃用 volatile 的大部分用法，这次弃用保住了 volatile 有用的部分，去掉的是可疑的、已经坏掉的部分。弃用（deprecated）的意思是语言给用法标上记号劝退，编译器的警告也跟着来了，再往下一步才是真正的删除。动机的原文更直接：

```text
This paper aims at breaking at compile-time code which is today subtly
broken at runtime or through a compiler update.
```

目标是让那些如今在运行期悄悄坏掉、或者随一次编译器更新就坏掉的代码，改在编译期就把错给报了出来。具体弃用了哪些东西，cppreference 同页 Notes 给了清单，咱们逐字搬来：

```text
Some uses of volatile are deprecated:
- lvalue of volatile type as operand of built-in increment/decrement operators;
- lvalue of volatile type as left operand of built-in direct assignment,
  unless the direct assignment expression appears in an unevaluated context
  or is a discarded-value expression;
- volatile object type as function parameter type or return type;
- volatile qualifier in structured binding declaration.
```

咱们逐条对号：清单里列着 volatile 上的 `++` 与 `--`、带豁免的直接赋值，外加 volatile 的函数形参与返回类型，结构化绑定里的 volatile 也在内。复合赋值没有单列在 cppreference 的四个条目里，它出自提案 P1152R4 自己的改动清单，E1 为 volatile 的 `E1 op= E2` 也一并标了弃用。第二条值得咱们细看，直接赋值也被列入了弃用的范围，但是带有一个豁免：赋值表达式处在未求值的上下文，或者它是弃值表达式的时候不罚。弃值表达式说人话就是表达式的值没人接走，语句写在那儿就完了。v4_race.cpp 里的 `++counter` 两头都不沾，所以 GCC 16 默认就报了警告，就是咱们在上一篇里贴过的两条 -Wvolatile。拆三条语句的那版写的是 `counter = v`，加屏障的那版写的是 `counter = v + 1`，都是语句级的赋值，值是没人接的，咱们照着清单查下来，它们都落不进弃用的范围。第一个主题里写 NoisyIter 的时候，咱们特意用读、写、写三步去绕开 `side_count++`，避的就是这一点弃用，这回您见到警告的真身了。

弃用是不等于删除的，这些写法您今天拿去编译仍能通过，只是编译器会把疑问摆到您的面前。方向也是明白的：提案自己说弃用保住了有用的部分，指的正是设备访问那一类咱们马上要去看的用场，而多线程通信的那一侧，标准亲口说了不适合。

## 主场：会自己变的内存

咱们来看 volatile 名正言顺的地方：内存映射的设备寄存器，它的行话叫法是 mmio（全称 memory mapped I/O），做法就是把设备的寄存器映射成内存地址，程序对地址的读写就等于对设备的读写。设想一段轮询的代码：设备的状态寄存器里，就绪标志是由设备自己置位的，咱们的程序一圈一圈读它、等它变。这段代码里改值的东西，既没有别的线程，也没有本程序的任何一行，而是编译器完全看不见的硬件。咱们用两个只差指针类型的函数模拟这个场景：

```cpp
// v6_mmio.cpp: 设备状态寄存器轮询 —— volatile 指针 vs 普通指针的汇编对照
// 只编译不运行;两个函数均为外部链接,函数体必须保留

int poll_volatile(volatile int* status, int n) {
    int s = 0;
    for (int i = 0; i != n; ++i)
        s += *status;   // volatile 读:期望每次迭代一次装载
    return s;
}

int poll_plain(int* status, int n) {
    int s = 0;
    for (int i = 0; i != n; ++i)
        s += *status;   // 普通读:期望装载被外提/合并
    return s;
}
```

```bash
g++ -S -O2 -std=c++20 -fno-asynchronous-unwind-tables v6_mmio.cpp -o v6_mmio.s
```

```asm
_Z13poll_volatilePVii:
.LFB0:
	.cfi_startproc
	testl	%esi, %esi
	je	.L4
	xorl	%eax, %eax
	xorl	%edx, %edx
	.p2align 4
	.p2align 4
	.p2align 3
.L3:
	addl	$1, %eax
	addl	(%rdi), %edx
	cmpl	%eax, %esi
	jne	.L3
	movl	%edx, %eax
	ret
	.p2align 4,,10
	.p2align 3
.L4:
	xorl	%edx, %edx
	movl	%edx, %eax
	ret
	.cfi_endproc

_Z10poll_plainPii:
.LFB1:
	.cfi_startproc
	testl	%esi, %esi
	je	.L9
	movl	(%rdi), %eax
	imull	%esi, %eax
	ret
	.p2align 4,,10
	.p2align 3
.L9:
	xorl	%eax, %eax
	ret
	.cfi_endproc
```

普通指针版的推理咱们在第一篇的 v1_loop_plain 实验里已经见过：循环里没有可能写这块内存的操作，重复读的值都等价，于是循环就被换成了一条 `movl (%rdi), %eax` 加一条 `imull %esi, %eax`，读一次、乘个 n 就算收了工。开头的 `testl %esi, %esi` 与 `je` 是 n 为 0 时的提前返回，循环的条件由结尾的 `cmpl` 拿计数和 n 对比。要是 `(%rdi)` 的背后真是设备，设备后来的每一次状态变化，这唯一的一次装载就都见不到了，咱们就会看着轮询拿着一个旧值一路算到底。volatile 指针版的每轮是一条 `addl (%rdi), %edx`，每一圈的装载都要经过指针重新来，设备改了值，下一圈就能读到新的。指针是运行时传进来的参数，汇编里能看见的只有 `(%rdi)`，编译期根本不知道它指到了哪里，但类型上的 volatile 把禁令跟着指针带了进来。

值不变的时候，两个版本跑起来会有差别吗？咱们补一个能跑的验证：用 mmap 申请一页合法的内存充当寄存器，把值设成了 7：

```cpp
// v6_mmio_run.cpp: mmap 一页合法内存充当"设备寄存器",行为用 printf 消费
#include <sys/mman.h>
#include <unistd.h>
#include <cstdio>

int poll_volatile(volatile int* status, int n) {
    int s = 0;
    for (int i = 0; i != n; ++i)
        s += *status;
    return s;
}

int poll_plain(int* status, int n) {
    int s = 0;
    for (int i = 0; i != n; ++i)
        s += *status;
    return s;
}

int main() {
    long pgsz = sysconf(_SC_PAGESIZE);
    void* page = mmap(nullptr, pgsz, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (page == MAP_FAILED) {
        std::perror("mmap");
        return 1;
    }

    auto status = static_cast<volatile int*>(page);
    *status = 7;   // 模拟设备把状态寄存器置为 7

    int sv = poll_volatile(status, 10);
    int sp = poll_plain(const_cast<int*>(static_cast<volatile int*>(page)), 10);
    std::printf("poll_volatile = %d, poll_plain = %d (7*10 = 70)\n", sv, sp);
    return 0;
}
```

```bash
g++ -O2 -std=c++20 v6_mmio_run.cpp -o v6_mmio_run && ./v6_mmio_run
```

```text
poll_volatile = 70, poll_plain = 70 (7*10 = 70)
```

两个版本跑出来的都是 70。单线程里的值没有变过，合并装载与反复装载算出的结果也就相同，这顺带消除了一个疑问：volatile 版会不会读出别的值。传参时那串 `static_cast` 与 `const_cast` 干的只是在 volatile 指针和普通指针之间转换类型的活，让两个函数各拿各的形参类型，看的都是同一页内存。行为的差异只在值真的会变时显形，而差异长什么样，咱们已经在汇编里看得清清楚楚了。

这套东西是嵌入式开发的日常。您要是翻开 CMSIS 的设备头文件，会看到寄存器结构体的成员清一色是 `__IO uint32_t`，`__IO` 宏展开来的就是 volatile，GPIO 的配置、状态、数据寄存器，每一个都是会自己变的内存。嵌入式线 F103 有一篇 [操作的本质：咱们写的是代码，动的是地址](../../../vol8-domains/embedded/f103/01-led/02-mmio-essence.md)，把 `GPIOC->CRH |= x` 一路展开到 `*(volatile uint32_t*)0x40011004` 的读改写，反汇编里的 ldr、orr、str 三条挨个对过，验证走的是 Renode 仿真加 host 上能跑的示例，您能亲眼看到等待循环在 -O2 下被整个删掉的样子。volatile 在嵌入式线上担的是正确性的前提，跟性能是不沾边的：少了它的场合，寄存器的读被合并，设备的状态变化程序就看不见了。

## 判断法：改值的对面是谁

咱们把三篇收拢成一条判断法，要问的问题就两个：头一个，这个对象的值除了本线程的代码还有谁会改？另一个要问的是，读它写它的代码跑在几个线程里？

- 改写者是设备，或者异步的信号处理器，读写全在单线程里：用 volatile。编译器看不见那些改写者，咱们用关键字替它把值会变这件事说进去。读、写直来直去地写，`++`、复合赋值别用在 volatile 上，C++20 已经弃用，GCC 会警告。
- 改写者是另一个线程：换 `std::atomic`。原子性靠 lock 前缀在硬件层兑现，可见性与顺序靠内存序参数声明，这些 volatile 一个都没承诺，那条分界线长什么样您已经在上一篇的机器码里见过了。
- 改写者只有本线程，也没有异步介入：什么都别加，让编译器干活。普通变量的合并与消除正是您要的优化，01 里那个 volatile int sink 是观察手段，不是写法榜样。

收尾咱们把三篇里最值钱的证据再摆一次。同样是两线程各十万次自增的场景，volatile 版与 atomic 版线程体里做自增的指令，咱们各取一条：

```asm
addl   $0x1,0x2cdd(%rip)        # 4064 <counter>
lock addl $0x1,0x2c5c(%rip)        # 4064 <counter>
```

一条来自 volatile 的版本，一条来自 atomic 的版本，指令的本体一模一样（counter 的落址两边相同，汇编的注释里都写着 4064，偏移差 129 恰好等于两条指令各自的 rip 基准之差，rip 相对寻址拿的正是自己下一条指令的位置当基准），只差的一个词是 lock。volatile 说话的对象是编译器，它的话全部落在编译期，而机器码里一无所见，lock 前缀说话的对象是 CPU，它的话就写在指令里。往后您再看见谁拿 volatile 当线程同步教人，您把这两行汇编摆给他看。
