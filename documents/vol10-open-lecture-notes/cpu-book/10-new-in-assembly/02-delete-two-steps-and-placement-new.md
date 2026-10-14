---
title: "delete 的两步与 placement new：归程和省掉的分配"
chapter: 10
order: 2
tags: [host, cpp-modern, intermediate]
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 17]
description: "delete 的两步里析构走在前、operator delete 带着尺寸收尾，构造抛异常时着陆区先还内存再继续展开，placement new 则留下调用、抽掉分配的实质"
reading_time_minutes: 12
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
---

# delete 的两步与 placement new：归程和省掉的分配

环境、工具和贴文的读法都跟[上一篇](01-allocation-from-operator-new.md)相同，您要复跑就照它来。分配和构造的两步上一篇已经拍实，剩下的事是把内存还回去。

## 归程：delete 的两步

使用的期限到了，内存也该还回去了。delete 的两步与 new 的两步对称，咱们想看清头一步的话，类型得有一个非平凡的析构函数，所以换了一个会打印的素材：

```cpp
// N4: delete 的两步 —— 析构 + operator delete
#include <cstdio>

struct Resource {
    int id;
    explicit Resource(int i) : id(i) { std::printf("Resource(%d) ctor\n", i); }
    ~Resource() { std::printf("Resource(%d) dtor\n", id); }
};

int main() {
    Resource* r = new Resource(7);
    volatile int sink = r->id;
    (void)sink;
    delete r;
    return 0;
}
```

```text
$ g++ -O0 -o n4 n4.cpp && ./n4
Resource(7) ctor
Resource(7) dtor
```

分配侧的两步咱们上一篇已经认得了，咱们直接看删除侧。`main` 的汇编如下，构造函数和析构函数的函数体删节了，其余的部分逐字：

```asm
main:
	pushq	%rbp
	movq	%rsp, %rbp
	pushq	%r13
	pushq	%r12
	pushq	%rbx
	subq	$24, %rsp
	movl	$4, %edi            # sizeof(Resource) = 4
.LEHB0:
	call	_Znwm@PLT           # 第一步:operator new(4)
.LEHE0:
	movq	%rax, %rbx          # rax → rbx 保管指针
	movl	$1, %r13d
	movl	$7, %esi            # 实参 7
	movq	%rbx, %rdi          # this
.LEHB1:
	call	_ZN8ResourceC1Ei    # 第二步:Resource::Resource(this, 7)
.LEHE1:
	movl	$0, %eax
	movq	%rbx, -40(%rbp)
	testb	%al, %al
	je	.L4                     # 恒跳,紧随的是不可达清理死代码
	movl	$4, %esi
	movq	%rbx, %rdi
	call	_ZdlPvm@PLT
.L4:
	movq	-40(%rbp), %rax
	movl	(%rax), %eax
	movl	%eax, -44(%rbp)     # sink = r->id
	movl	-44(%rbp), %eax
	movq	-40(%rbp), %rbx
	testq	%rbx, %rbx          # delete 的空指针检查
	je	.L5
	movq	%rbx, %rdi          # 析构的 this
	call	_ZN8ResourceD1Ev    # 第一步(删除侧):~Resource()
	movl	$4, %esi            # sized delete 尺寸
	movq	%rbx, %rdi          # operator delete 的指针参数
	call	_ZdlPvm@PLT         # 第二步(删除侧):operator delete(p, 4)
.L5:
	movl	$0, %eax
	jmp	.L10
.L9:                              # 异常着陆区:构造抛出时先释放内存再继续展开
	movq	%rax, %r12
	testb	%r13b, %r13b
	je	.L8
	movl	$4, %esi
	movq	%rbx, %rdi
	call	_ZdlPvm@PLT
.L8:
	movq	%r12, %rax
	movq	%rax, %rdi
.LEHB2:
	call	_Unwind_Resume@PLT
.LEHE2:
.L10:
	addq	$24, %rsp
	popq	%rbx
	popq	%r12
	popq	%r13
	popq	%rbp
	ret
```

删除侧的两步在 `.L5` 之前排得整整齐齐：打头的是 `call _ZN8ResourceD1Ev`，析构的调用在前，收尾的是 `call _ZdlPvm@PLT`，还内存的调用在后。顺序是有意义的，析构函数要读写的是这块内存，您看它打印 id 就知道了，内存一旦还了回去就不能再碰，所以析构必须走在前面。再往前一步还有 `testq %rbx, %rbx` 的空指针检查，对应着语言里 delete 一个空指针是合法无操作的规定，cppreference 的 [operator delete 页面](https://en.cppreference.com/w/cpp/memory/new/operator_delete)写的是：

```text
The effects of the default versions are: 1) If ptr is null, does nothing.
Otherwise, reclaims the storage allocated by the earlier call to operator new.
```

指针为空就什么都不做，否则收回早前 operator new 分配的存储。这段 `testq` 加 `je` 就是规定落在指令上的样子，也是您在 -O0 汇编里辨认 delete 语句本体的记号。

符号上有个不少资料都过时了的细节。您看删除侧调的是 `_ZdlPvm`，解码出来的本名是 `operator delete(void*, unsigned long)`，第二个参数是它的尺寸，放在 esi 里的就是 4。这是带尺寸的释放，C++14 起进的标准特性，cppreference 的记录是：

```text
void operator delete ( void* ptr, std::size_t sz ) noexcept; (5) (since C++14)

[__cpp_sized_deallocation] [201309L] (C++14) Sized deallocation
```

GCC 16 在默认的 gnu++20 语境下、删除点尺寸已知的时候，发的就是带尺寸版，上一篇 N1 里 Point 的 delete 也是它，esi 装的 8 还是那个 sizeof。无尺寸的 `_ZdlPv` 只在尺寸不可得一类的场景才出现，比如经基类指针删除多态对象的情形。您读汇编时看到 `_ZdlPvm`，心里默念的应该是 operator delete 加一个尺寸参数。

咱们再回到上一篇的 N1 对照，那里还藏着省事的证据。Point 的析构函数是平凡的，N1 的删除侧就没有析构的调用，直接一步 `operator delete` 就完事了。删除侧的两步是以非平凡析构为前提的，平凡析构的类型只有后面的那一步。这和 05 里隐式构造函数不生成独立调用的道理相同，编译器不为无事可做的析构生成调用。

清单的尾部还有一段 `.L9`，它是本站的压轴证据。`.LEHB1` 和 `.LEHE1` 这对标号圈出的区间，是异常表登记的监控范围，构造函数若在这个区间里抛了异常，栈展开的机制就会把控制送到 `.L9` 这个着陆点。咱们看它干的事：`testb %r13b, %r13b` 查一个标志，r13d 在 operator new 成功返回之后就被置成了 1，含义是内存已经到了手、需要善后，所以着陆之后 `call _ZdlPvm@PLT` 把刚分配的 4 字节还回去，再往后的 `call _Unwind_Resume@PLT` 负责继续向上展开，让外层的 catch 接手。new 表达式分配的内存不会因为构造抛异常就漏掉，这个语言层的约定，今天您看到的就是它的指令级实现。`_Unwind_Resume` 背后的栈展开机制，[09](../09-manual-backtrace-with-gdb/02-no-frame-pointer-no-symbols.md) 里咱们靠着 unwind 表爬过调用链，同一套设施在这里干的是送异常回家的活。

回看上一篇 N1 里那段恒不可达的 `_ZdlPvm`，您现在能说出它的身份了：它和 `.L9` 是同一件事的两种生成形态，都是为构造抛异常准备的。在 N4 的清单里数 `_ZdlPvm`，正好是三处：`.L4` 前面那段是死的清理，`.L5` 前面那段是真正的 delete 语句，`.L9` 里的那段只在异常时到达。Point 的构造函数不会抛异常，GCC 在 -O0 下照章发了测试与分支，但测试的条件被常量置了零，路径就死了。Resource 的构造同样不会抛，所以 `.L4` 前那段也是死的，真正能到达的善后在 `.L9`。咱们要分辨它们，判别法还是同一条：看 `testq` 的空指针检查认语句本体，看 `.LEHB` 的标号认异常路径。

## 变体的一站：placement new，分配的消失方式

旅程走到了这里，两步走的框架已经立住了，接下来咱们看两个变体怎么在这个框架上动手脚。咱们头一个要看的是 placement new，它是 new 的一个变体：内存由您自己出，new 表达式只负责在您给的内存上跑构造这一步。标准库为它配的分配函数是 `operator new(size_t, void*)`，收一个字节数和一个地址。咱们在栈上开一块裸缓冲来试：

```cpp
// N5: placement new —— 只有构造,没有分配
#include <new>

struct Point {
    int x;
    int y;
    Point(int x_, int y_) : x(x_), y(y_) {}
};

int main() {
    alignas(Point) char buf[sizeof(Point)];   // 栈上裸缓冲
    Point* p = new (buf) Point(3, 4);         // placement new
    volatile int sink = p->x + p->y;          // 防优化消费
    (void)sink;
    p->~Point();                              // 手动析构,无 delete
    return 0;
}
```

```text
$ g++ -O0 -fno-asynchronous-unwind-tables -S -o n5.s n5.cpp
```

```asm
main:
	pushq	%rbp
	movq	%rsp, %rbp
	pushq	%rbx
	subq	$40, %rsp
	movq	%fs:40, %rax        # 栈 canary(栈上 char 缓冲触发)
	movq	%rax, -24(%rbp)
	xorl	%eax, %eax
	leaq	-32(%rbp), %rax     # buf 的地址
	movq	%rax, %rsi          # 第二实参:缓冲地址
	movl	$8, %edi            # 第一实参:字节数(仍传,但无人使用)
	call	_ZnwmPv             # placement 版 operator new(size, buf)
	movq	%rax, %rbx
	movl	$4, %edx
	movl	$3, %esi
	movq	%rbx, %rdi
	call	_ZN5PointC1Eii      # 构造:this 就是 buf
	movl	$0, %eax
	movq	%rbx, -40(%rbp)
	testb	%al, %al
	je	.L6                     # 恒跳,死代码开始
	leaq	-32(%rbp), %rax
	movq	%rax, %rsi
	movq	%rbx, %rdi
	call	_ZdlPvS_            # (不可达)placement 配对 delete(void*,void*)
.L6:
	movq	-40(%rbp), %rax
	movl	(%rax), %edx
	movq	-40(%rbp), %rax
	movl	4(%rax), %eax
	addl	%edx, %eax
	movl	%eax, -44(%rbp)     # sink
	movl	-44(%rbp), %eax
	movl	$0, %eax
	movq	-24(%rbp), %rdx     # canary 校验
	subq	%fs:40, %rdx
	je	.L8
	call	__stack_chk_fail@PLT
.L8:
	movq	-8(%rbp), %rbx
	leave
	ret
```

咱们跟上一篇的 N1 并排看，会发现一个乍看矛盾的景象。说好的 placement new 省掉分配这一步，汇编里却还有一次 `call _ZnwmPv` 的调用，而且 edi 里照样装着 8。这个调用咱们得分开看。头一件事是它没有 `@PLT` 的后缀，属于直接调用：placement 版 operator new 的一个副本被编进了可执行文件，`nm -C` 能在文件里找到它的符号，`objdump` 把它的函数体翻出来是这个样子：

```text
00000000000011c7 <_ZnwmPv>:
    11c7:	55                   	push   %rbp
    11c8:	48 89 e5             	mov    %rsp,%rbp
    11cb:	48 89 7d f8          	mov    %rdi,-0x8(%rbp)
    11cf:	48 89 75 f0          	mov    %rsi,-0x10(%rbp)
    11d3:	48 8b 45 f0          	mov    -0x10(%rbp),%rax
    11d7:	5d                   	pop    %rbp
    11d8:	c3                   	ret
```

整个函数的有效动作只有一条：把第二个参数从栈上原样搬回 rax，然后就返回了。第一个参数就是 edi 里装的那个 8，进了函数就被存进栈槽，之后咱们就再没见过它被使用。这是一个进什么地址就出什么地址的恒等函数。cppreference 对它的原话是：

```text
Such allocation functions are known as "placement new", after the standard
allocation function void* operator new(std::size_t, void*), which simply
returns its second argument unchanged.
```

所以严谨的说法是：placement new 在 -O0 的汇编里不是只剩构造一步，对 operator new 的调用还在，只是被调用的函数不做任何分配，它把您给的地址原样送回来，构造函数紧跟着在这个地址上动土。少掉的是向 malloc 要内存这一实质，少不掉的是调用这一形式。您要跟别人讲这件事，站得住的讲法有两种：一种是说没有分配发生，另一种是把上面的 objdump 摆出来讲清恒等函数，唯独别说汇编里没有 operator new 的调用，汇编会打您的脸。

清单的另外几处咱们也顺路收个尾。buf 是栈上的 char 数组，触发了栈保护的 canary，`%fs:40` 那对指令您在 01 的环境说明里见过，属于正常的现象。`p->~Point()` 是手动的析构，Point 的析构平凡，所以汇编里零调用，而 placement new 配的 delete 本来就不存在，buf 随着栈帧的回收一起消失，全程没有 operator delete 的事。死代码那段调的 `_ZdlPvS_` 是 placement 版的配对释放，同样到不了，判别的方法照旧。

这个例子还顺带演示了为什么观察一律用 -O0。咱们把同一份代码换成 -O2 编，main 就成了这样：

```asm
main:
	movl	$7, -4(%rsp)
	movl	-4(%rsp), %eax
	xorl	%eax, %eax
	ret
```

构造、求和、sink 的消费全部被编译期算掉了，`movl $7` 里那个 7 就是 3 加 4 的和。您要自己复现本篇的任何一块汇编，就请守住 -O0 的档位，-O2 下什么都看不见。placement 这个变体咱们到此看完，第二个变体 new[] 的块头里存着元素的个数，[下一篇](03-array-new-cookie-and-reverse-destruction.md)咱们去把它读出来。
