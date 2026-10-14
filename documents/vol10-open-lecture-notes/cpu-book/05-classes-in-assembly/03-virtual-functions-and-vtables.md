---
title: "你的类在汇编里长什么样：虚函数与虚表"
chapter: 5
order: 3
tags: [host, cpp-modern, intermediate]
difficulty: intermediate
platform: host
cpp_standard: [11, 17]
description: "vptr 让对象胖出 8 字节，gdb 把 Base 与 Derived 的虚表翻出来，对上地址点与同类共享的一张表，再看一次虚调用的三步：装 vptr、取槽位、间接 call"
reading_time_minutes: 15
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
---

# 你的类在汇编里长什么样：虚函数与虚表

咱们接着用跟[第一篇](01-this-pointer-and-member-offsets.md)一模一样的环境往下做。前两篇说的都是“对象就是数据加普通函数调用”，虚函数则是 C++ 给对象加的最后一样真家伙。咱们把同一个两 int 的结构做两份，一份不带虚函数的，另一份带一个虚函数的：

```cpp
// e4.cc —— 虚函数对象的布局：vptr 让对象变大
#include <cstdio>

struct Plain {  // 没有虚函数
    int x;
    int y;
};

struct Virt {  // 有一个虚函数
    int x;
    int y;

    virtual void poke();

    Virt(int a, int b) : x(a), y(b) {}
};

void Virt::poke() {}

static_assert(sizeof(void*) == 8, "本文只谈 64 位");
static_assert(sizeof(Plain) == 8, "两个 int，紧凑无对齐尾巴");
static_assert(sizeof(Virt) == 16, "vptr(8) + x(4) + y(4)");

int main() {
    std::printf("sizeof(void*) = %zu\n", sizeof(void*));
    std::printf("sizeof(Plain) = %zu\n", sizeof(Plain));
    std::printf("sizeof(Virt)  = %zu\n", sizeof(Virt));
    std::printf("alignof(Virt) = %zu\n", alignof(Virt));

    Virt v(1, 2);
    Virt* volatile vp = &v;  // volatile 消费，防对象被优化掉
    return (vp->x + vp->y) - 3;
}
```

```text
$ g++ -O0 -S e4.cc -o e4.s     # 虚函数实验保持默认标志（含 unwind 表）
$ g++ -O0 e4.cc -o e4 && ./e4
sizeof(void*) = 8
sizeof(Plain) = 8
sizeof(Virt)  = 16
alignof(Virt) = 8
exit=0
```

三条 `static_assert` 在编译期就全部通过了，运行时 printf 又报了一遍：Plain 的大小是 8 字节，Virt 的大小是 16 字节，多出来的 8 字节就是 vptr。vptr 的全名是虚表指针，您把它理解成对象头上多出来的一个指针字段，它指向所属类的虚表。虚表则是编译期为每个带虚函数的类生成的一张函数指针数组，表里按顺序登记着这个类的各个虚函数的入口地址。后文咱们还要反复用到 vptr 和虚表，所以把白话版记在这里：对象里存指针，指针指到的是一张表，表里存的是函数地址。

多出来的 8 字节住在哪、什么时候写进去，咱们让构造函数的汇编来招：

```asm
_ZN4VirtC2Eii:
.LFB3:
	.cfi_startproc
	pushq	%rbp
	.cfi_def_cfa_offset 16
	.cfi_offset 6, -16
	movq	%rsp, %rbp
	.cfi_def_cfa_register 6
	movq	%rdi, -8(%rbp)
	movl	%esi, -12(%rbp)
	movl	%edx, -16(%rbp)
	leaq	16+_ZTV4Virt(%rip), %rdx   # ← vtable 符号 +16 的地址
	movq	-8(%rbp), %rax
	movq	%rdx, (%rax)               # ← vptr 写进 this+0
	movq	-8(%rbp), %rax
	movl	-12(%rbp), %edx
	movl	%edx, 8(%rax)              # x = a → this+8
	movq	-8(%rbp), %rax
	movl	-16(%rbp), %edx
	movl	%edx, 12(%rax)             # y = b → this+12
	nop
	popq	%rbp
	.cfi_def_cfa 7, 8
	ret
	.cfi_endproc
```

咱们对比上一篇里 e2 那个朴素构造函数，您能挑出三处变化。头一处的变化在函数体开头多出的一对指令：`leaq 16+_ZTV4Virt(%rip), %rdx` 把虚表符号加 16 的地址算了出来，`movq %rdx, (%rax)` 把它写进了 this+0。构造函数进门干的第一件正事就是装 vptr，排在了用户初始化列表的前面。`_ZTV4Virt` 是 vtable for Virt 的修饰名，objdump -C 翻译符号表的时候，它显示出来的样子就是 vtable for Virt。至于加的那个 16，咱们把这个数记在这里，下一节咱们拿 gdb 把它验个明白。第二处的变化是 x 和 y 的偏移从 +0 和 +4 挪到了 +8 和 +12，vptr 把成员往后顶了 8 字节。第三处是写法上的小事，binutils 2.47 的 AT&T 输出把符号加常数写成 `16+_ZTV4Virt(%rip)`，常数写在了前面，旧资料里常见的 `_ZTV4Virt+16` 与它同义，您对照时别当成两个东西。

虚表的本体也在 `e4.s` 里，咱们要的数据定义一共三行：

```asm
	.weak	_ZTV4Virt
	.section	.data.rel.ro.local._ZTV4Virt,"awG",@progbits,_ZTV4Virt,comdat
	.align	8
	.type	_ZTV4Virt, @object
	.size	_ZTV4Virt, 24
_ZTV4Virt:
	.quad	0                       # 槽 -2：offset-to-top（完整对象为 0）
	.quad	_ZTI4Virt               # 槽 -1：RTTI typeinfo 指针
	.quad	_ZN4Virt4pokeEv          # 槽 0： poke —— vptr 指的就是这里
```

咱们看 `.size` 报的 24 字节正好是三个 quad（quad 是 8 字节的数据单元，一条 `.quad` 伪指令安放的就是一个）。前两槽放的不是函数指针：第一槽放的是 offset-to-top，存的内容是“从 vptr 所在位置到对象顶部的位移”，完整对象里它的值是 0，用处要到多继承挪动指针的时候才显出来。第二槽放的是 typeinfo 指针，typeinfo 就是运行时的类型信息，`dynamic_cast` 和 `typeid` 靠的都是它。真正的函数指针从第三个 quad 开始，poke 的地址就登记在那里。这也解释了构造函数里的 16：表开头的两槽各占 8 字节，8 加 8 得到的是 16，vptr 跳过了这两槽，直接指向了第一个函数指针。

## 把虚表整个 dump 出来

e4 一节咱们贴过 Virt 的虚表数据定义，不过那还是汇编里的静态符号，本节咱们把 Base 与 Derived 的表放到运行时翻出来看。03 结尾约过的那套 gdb 读法，咱们也在这里兑现：断点一停，对象里存的指针、虚表里的槽位，咱们想看哪个看哪个。素材是一个小小的继承体系：Base 声明了两个虚函数，Derived 覆盖了 second，又新添了一个 third，咱们另造两个 Base 对象、一个 Derived 对象当活标本：

```cpp
// e5.cc —— 虚表 dump：同类对象共享同一张表
struct Base {
    int tag;

    Base(int t) : tag(t) {}
    virtual const char* first();
    virtual const char* second();
};

const char* Base::first() { return "Base::first"; }
const char* Base::second() { return "Base::second"; }

struct Derived : Base {
    Derived() : Base(2) {}
    const char* second() override;  // 只覆盖第二个
    virtual const char* third();    // 再加一个新的
};

const char* Derived::second() { return "Derived::second"; }
const char* Derived::third() { return "Derived::third"; }

Base b1(1);
Base b2(2);  // 与 b1 同类：应共享同一张虚表
Derived d;

int main() {
    return (b1.tag + b2.tag + d.tag) - 4;
}
```

```text
$ g++ -O0 -g e5.cc -o e5
$ ./e5; echo "exit=$?"
exit=1
$ objdump -t -C e5 | grep -i 'vtable\|typeinfo'
0000000000000000       O *UND*	0000000000000000              vtable for __cxxabiv1::__class_type_info@CXXABI_1.3
0000000000003da8  w    O .data.rel.ro	0000000000000020              vtable for Base
0000000000003dc8  w    O .data.rel.ro	0000000000000018              typeinfo for Derived
0000000000000000       O *UND*	0000000000000000              vtable for __cxxabiv1::__si_class_type_info@CXXABI_1.3
0000000000002040  w    O .rodata	0000000000000009              typeinfo name for Derived
0000000000003d80  w    O .data.rel.ro	0000000000000028              vtable for Derived
0000000000003de0  w    O .data.rel.ro	0000000000000010              typeinfo for Base
0000000000002049  w    O .rodata	0000000000000006              typeinfo name for Base
```

咱们把一处容易引起误会的东西按住：程序的退出码是 1，那是 main 返回值算出来的 1 加 2 加 2 减 4，属于设计好的返回值，而非崩溃。然后咱们看符号表。vtable 和 typeinfo 都在里面躺着的，尺寸也对得上：vtable for Base 的 32 字节，正好是头两槽加两个函数槽凑成的大小，vtable for Derived 的 40 字节，是头两槽加三个函数槽的容量。表里的文件偏移 0x3da8 和 0x3d80 加上加载基址，就是它们运行时的地址，马上就能在 gdb 里对上了。

三个对象走的都是全局变量，main 之前就构造完了，咱们把断点下在 main，咱们一进场就能验。会话的原文如下，开头结尾的工具问答删了，中间的每条命令和输出都是逐字的：

```text
(gdb) set pagination off
(gdb) break main
Breakpoint 1 at 0x1165: file e5.cc, line 27.
(gdb) run
Starting program: /tmp/cpu-book-05/e5
[Thread debugging using libthread_db enabled]
Using host libthread_db library "/usr/lib/libthread_db.so.1".

Breakpoint 1, main () at e5.cc:27
27	    return (b1.tag + b2.tag + d.tag) - 4;
(gdb) p &'vtable for Base'
$1 = (<data variable, no debug info> *) 0x555555557da8 <vtable for Base>
(gdb) p &'vtable for Derived'
$2 = (<data variable, no debug info> *) 0x555555557d80 <vtable for Derived>
(gdb) info vtbl b1
vtable for 'Base' @ 0x555555557db8 (subobject @ 0x555555558020):
[0]: 0x55555555511a <Base::first()>
[1]: 0x55555555512c <Base::second()>
(gdb) info vtbl b2
vtable for 'Base' @ 0x555555557db8 (subobject @ 0x555555558030):
[0]: 0x55555555511a <Base::first()>
[1]: 0x55555555512c <Base::second()>
(gdb) info vtbl d
vtable for 'Derived' @ 0x555555557d90 (subobject @ 0x555555558040):
[0]: 0x55555555511a <Base::first()>
[1]: 0x55555555513e <Derived::second()>
[2]: 0x555555555150 <Derived::third()>
(gdb) p/x *(void**)&b1
$3 = 0x555555557db8
(gdb) p/x *(void**)&b2
$4 = 0x555555557db8
(gdb) p/x *(void**)&d
$5 = 0x555555557d90
(gdb) p *(void**)&b1 == *(void**)&b2
$6 = true
(gdb) p/x (char*)&'vtable for Base' + 16
$7 = 0x555555557db8
(gdb) quit
```

整个会话里有三个层次的可看之处，咱们从最解气的一个说起。`p *(void**)&b1 == *(void**)&b2` 直接问 gdb：两个对象头部存的指针相等吗？回答是 `$6 = true`，短短一行的判词。b1 和 b2 是两个不同的对象，`info vtbl` 里它们的 subobject 地址确实不同（0x...8020 对 0x...8030），但虚表地址同为 0x555555557db8。同类对象共享的是同一张虚表，每个对象存的都是同一个地址。这也顺带澄清了虚函数额外开销里最容易被夸大的部分：表是类一张，而不是对象一张。

咱们看第二个层次，上一节悬下的那个 16。`$1` 给出 vtable for Base 符号自己的地址 0x555555557da8，`$7` 算出这个地址加 16 等于 0x555555557db8，而 `$3` 和 `$4` 里，b1、b2 对象头部存的 vptr 正是 0x555555557db8。三个数字连成了一条等式：对象里的 vptr 等于虚表符号加 16，上一节构造函数里的 `leaq 16+_ZTV4Virt(%rip)` 在这里拿到了运行时的对证。vptr 并不指向表的开头，它指向跳过 offset-to-top 和 typeinfo 两槽之后的头一个函数指针，这个位置有个正式的名字，叫地址点。

第三个层次是 Derived 的槽位安排，`info vtbl d` 的三行正好把三种动作都摆了出来。槽 0 还是原来的 `Base::first()`，继承的动作就是照原样搬运，派生类把基类的表项原封不动地搬了过来。槽 1 换成了 `Derived::second()`，覆盖就是把那一槽的指针换成自己的函数。槽 2 是新添的 `Derived::third()`，声明放在派生类里的新函数，追加的位置就在表尾。您再回头看两张表的符号尺寸：32 字节对 40 字节，差出来的那个槽就是 third。

虚表布局这一块咱们给绘图管线留一个图位，图待补：内容为 vtable 符号从 offset-to-top、typeinfo 两槽到函数槽区的整体布局，标出地址点的位置，以及对象头部 vptr 指向地址点的相对关系，后续由本站的绘图管线补上。

这套布局写在一部叫 Itanium C++ ABI 的规范里。ABI 咱们在 03 认过这个词，是调用方与被调方共同遵守的二进制接口约定。Itanium C++ ABI 管的就是 C++ 层面，虚表怎么排、构造析构的符号怎么起名都归它管，GCC 在 Linux x86-64 上实现的就是它。关键句出自规范的 [2.5 节 Virtual Table Layout](https://itanium-cxx-abi.github.io/cxx-abi/abi.html#vtable)，咱们检索的日期是 2026 年 10 月 12 日。咱们从中挑出几条，从头两槽与地址点看起：

```text
The offset to top holds the displacement to the top of the object from
the location within the object of the virtual table pointer that
addresses this virtual table, as a ptrdiff_t. It is always present.

In a complete object virtual table, and therefore in all of its
primary base virtual tables, the value of this offset will be zero.

The typeinfo pointer points to the typeinfo object used for RTTI. It
is always present.

The virtual table address point points here, i.e. this is the virtual
table address contained in an object's virtual pointer.
```

咱们把它翻译过来：offset-to-top 槽存的是从 vptr 所在位置到对象顶部的位移，记的类型是 ptrdiff_t，而且永远在场，完整对象的虚表里它的值是零，对应咱们在 `e4.s` 里看到的 `.quad 0`。typeinfo 槽指向运行时类型信息用的 typeinfo 对象，而且也永远在场，对应的就是那行 `.quad _ZTI4Virt`。而虚表的地址点就落在 typeinfo 槽之后，对象 vptr 里装的就是这个地址，对应 gdb 里 `$7` 算出的那个加 16。规范里另外的一条讲地址点为什么不是表开头：

```text
In general, what we consider the address of a virtual table (i.e. the
address contained in objects pointing to a virtual table) may not be
the beginning of the virtual table. We call it the address point of
the virtual table. The virtual table may therefore contain components
at either positive or negative offsets from its address point.
```

咱们把它译成白话：平常说的“虚表地址”，也就是对象里存的那个指针，不一定是表的开头，规范给它起的名字叫地址点，表的组成项既可以落在地址点往正的方向，也可以落在负的方向。咱们在 `e4.s` 注释里写的槽 -2、槽 -1，负数就是这么来的。最后的两条原文管的是共享和排序：

```text
However, the virtual table pointers within all the objects (instances)
of a particular most-derived class point to the same set of virtual
tables.

The order of the virtual function pointers in a virtual table is
the order of declaration of the corresponding member functions in the
class.
```

前一句说同一个最派生类的所有对象，vptr 指向的是同一组虚表，gdb 里那句 `$6 = true` 就是它的落地。后一句说虚表里函数指针的排列次序跟类里声明的次序一致，first 在前 second 在后就是按它排的。规范原文与咱们今天的全部实测逐条对上了。谁说实现遵循规范看不见？咱们今天看见的就是它。

## 一次虚调用的三步

表也翻过了，咱们看调用。虚函数全部的特别之处，就浓缩在目标地址的来源上。咱们写一个最小的继承对，Base 和 Derived 各自给出 `who()` 的实现，再让一个函数 `call_who` 收下基类的指针、在上面调 `who()`：

```cpp
// e6.cc —— 多态调用三步：装 vptr → 取槽位 → 间接 call
#include <cstdio>

struct Base {
    virtual const char* who() { return "Base::who"; }
};

struct Derived : Base {
    const char* who() override { return "Derived::who"; }
};

const char* volatile observed = nullptr;  // volatile 消费返回值，防尾调用

__attribute__((noinline)) const char* call_who(Base* p) {
    const char* r = p->who();  // 全程序唯一的调用点，机器码只有一份
    observed = r;
    return r;
}

int main() {
    Base b;
    Derived d;

    std::printf("call_who(&b) -> %s\n", call_who(&b));
    std::printf("call_who(&d) -> %s\n", call_who(&d));
    return 0;
}
```

源码里有个不起眼的配角要提前交代。`observed` 这个 volatile 的全局可不是摆设。少了它会发生什么？编译器会把咱们那句 `return p->who();` 改成尾调用，call 也就变成了 jmp，咱们想看的 call 就没了。对 observed 的写就压在调用后面，尾调用的口子就这样被堵上了。`call_who` 的汇编如下：

```asm
_Z8call_whoP4Base:
.LFB4:
	.cfi_startproc
	pushq	%rbp
	.cfi_def_cfa_offset 16
	.cfi_offset 6, -16
	movq	%rsp, %rbp
	.cfi_def_cfa_register 6
	subq	$32, %rsp
	movq	%rdi, -24(%rbp)         # p 落栈
	movq	-24(%rbp), %rax         # 取回 p
	movq	(%rax), %rax            # ① 装载 vptr：p+0 处的 8 字节
	movq	(%rax), %rdx            # ② 取槽位：vptr+0 = 虚表 [0] 号槽 → rdx
	movq	-24(%rbp), %rax
	movq	%rax, %rdi              # this 作为第一个实参 → rdi
	call	*%rdx                   # ③ 间接 call：目标来自寄存器，不是立即数
	movq	%rax, -8(%rbp)
	movq	-8(%rbp), %rax
	movq	%rax, observed(%rip)    # volatile 消费，也压住了尾调用
	movq	-8(%rbp), %rax
	leave
	.cfi_def_cfa 7, 8
	ret
	.cfi_endproc
```

三步咱们逐一指认。第①步的 `movq (%rax), %rax`，从 p 指着的内存读 8 字节进 rax，读到的就是 vptr，这一步在装载对象头上的指针。第②步的 `movq (%rax), %rdx`，从 vptr 指着的内存再读 8 字节进 rdx，读到的就是虚表 0 号槽里登记的函数地址，这一步查的就是表。第③步的 `call *%rdx`，星号是间接调用的记号，跳转的目标并没有写死在指令里，它是 rdx 里此刻装着的值。普通调用是 `call _Z8call_whoP4Base` 这样目标明确的直达，虚调用则要查了表再上车。三步之后您再看 `movq %rax, %rdi`，this 照样走的是 rdi，交给目标函数的通道没有变。虚函数没有改 e1 立下的参数约定，变的只是目标地址从哪来。

咱们把两边的运行结果都拿到了：

```text
$ g++ -O0 -S e6.cc -o e6.s
$ g++ -O0 e6.cc -o e6 && ./e6
call_who(&b) -> Base::who
call_who(&d) -> Derived::who
```

调用的还是同一个 `call_who`，两次的落点也在同一个调用点上，却出了两个结果。差别埋在哪？咱们让 main 的汇编说话（节选，printf 的参数准备省略，指令是逐字的）：

```asm
main:
.LFB5:
	.cfi_startproc
	pushq	%rbp
	.cfi_def_cfa_offset 16
	.cfi_offset 6, -16
	movq	%rsp, %rbp
	.cfi_def_cfa_register 6
	subq	$32, %rsp
	movq	%fs:40, %rax
	movq	%rax, -8(%rbp)
	xorl	%eax, %eax
	leaq	16+_ZTV4Base(%rip), %rax    # b 的 vptr：Base 的表 +16
	movq	%rax, -24(%rbp)
	leaq	16+_ZTV7Derived(%rip), %rax # d 的 vptr：Derived 的表 +16
	movq	%rax, -16(%rbp)
	leaq	-24(%rbp), %rax
	movq	%rax, %rdi
	call	_Z8call_whoP4Base          # 同一个调用点，第一次
	...
	leaq	-16(%rbp), %rax
	movq	%rax, %rdi
	call	_Z8call_whoP4Base          # 同一个调用点，第二次
```

两条 `leaq` 各写各的：b 的 vptr 装的是 `_ZTV4Base` 加 16，d 的装的是 `_ZTV7Derived` 加 16。往后两个对象一前一后地送进了同一个 `call _Z8call_whoP4Base`，执行的指令一条不差，唯一不同的就是对象头部那 8 字节。三步走查到这里就齐了：多态在机器层面是一次数据驱动的分支，分支的条件压根不走 cmp 指令，它就是 vptr 里存的地址。这一段里 Base 和 Derived 用的是隐式构造函数，-O0 没有独立的构造函数可 call，编译器直接在定义点把 vptr 写进了对象，这跟 e4 构造函数里的做法互为印证：构造函数进门第一件正事装的是 vptr，不管您写的是哪种，vptr 都在构造的时候写定。

这次拐了弯的 call 贵不贵、-O2 拿它有没有办法，咱们下一篇接着看。
