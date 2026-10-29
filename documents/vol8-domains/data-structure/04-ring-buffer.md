---
title: "mini STL 实战(四):RingBuffer,最短的环形入门"
description: "固定容量的先进先出队列:单调读写指针只增不减,判空判满一次减法出结果,2 的幂把取模变成一条位与。汇编实测三种取模的机器码,覆盖式 push 的 Census 配平,还有连续区间版析构在环形布局上漏掉的那半圈。"
chapter: 4
order: 4
tags:
  - host
  - cpp-modern
  - intermediate
  - 容器
  - 循环缓冲区
  - 优化
difficulty: intermediate
platform: host
reading_time_minutes: 13
cpp_standard: [20]
prerequisites:
  - "mini STL 实战(三):Vector,五件套、异常安全与 concepts"
related:
  - "mini STL 实战(三):Vector,五件套、异常安全与 concepts"
  - "mini STL 实战(五):CircularDeque(一),环形不变量与迭代器"
---

# mini STL 实战(四):RingBuffer,最短的环形入门

上一篇结尾说,`% N` 这一步的取舍,Chromium 那边连“多取一次模都嫌贵”的注释都写好了。这不是笔者的转述,原文就在 `base/containers/ring_buffer.h` 里:

```cpp
  // This specialization of |IsFilledIndex| is a micro-optimization that enables
  // us to do e.g. `CHECK(IsFilledIndex(n))` without calling |BufferIndex|
  // twice. Since |BufferIndex| involves a % operation, it's not quite free at a
  // micro-scale.
```

翻成大白话:`BufferIndex` 里带着一个取模,在微观尺度上并不免费,所以人家宁可加一个小特化,也不肯在一次访问里把模取两遍。这份文件 2013 年就进了 Chromium,139 行,比咱们的第一面镜子 `vector_buffer.h` 还短。这一篇咱们顺着这个取模,把系列里最短的一件容器搓出来:`RingBuffer`,固定容量,先进先出,满了就挤掉最老的。

## 队列的两端都该是 O(1)

为什么要环形,咱们拿手头的 Vector 演一遍就明白了。队列要的是队尾进、队头出,Vector 只擅长一头:`push_back` 是 O(1),可出队得 `erase(begin())`,后面的元素整个往前挪一格,n 个元素就是 n 次搬迁。出一次队搬一轮,代价全花在挪元素上,队列这个用法把 Vector 的长处用反了。

另一类场景更极端,连出队都省了,只要最近的数据。飞行数据记录器就是典型:全程只写,容量固定,坠毁前最后几十秒的数据最要紧,更早的被新数据盖掉就盖掉。嵌入式的循环日志、异常采样、示波器的滚动窗口,全是同一个形状。咱们本篇的语义照这个定:push 永远成功,缓冲满了就挤掉最老的,这套叫覆盖式。另一种是拒绝式,满了就报状态拒绝写入,咱们留作变体练习,主线跑通之后改起来不费事。

Chromium 的 `RingBuffer` 就是覆盖式的现成参照。它的成员只有一份 `std::array<T, kSize>` 加一个只增不减的 `current_index_`,`SaveToBuffer` 写完就把计数加一,连出队的接口都没有,您只能读最近 kSize 个元素。存储直接用 `std::array`,所以 T 得能默认构造、能赋值。咱们沿用 stage1 的裸内存,对象的出生和死亡归自己管,另外咱们要支持 pop,这个差别马上会改变布局。

## 环形索引:物理下标 = 逻辑序号 % N

数组是直的,队列要的是圈,中间靠一对序号完成转换。咱们给 RingBuffer 两位成员:`read_pos` 和 `write_pos`,都是只增不减的整数,记的是读写进行到了第几个逻辑序号。写入的规程是,把元素放进 `write_pos % N` 对应的格子,然后 `write_pos` 加一。读出对称,读 `read_pos % N` 对应的格子,然后 `read_pos` 加一。序号自己永远往前走,绕圈全部交给取模,数组的首尾就这样在逻辑上接成了圈。

给您一张 N = 8 的对照表,序号从 0 走到 15,物理下标始终在 0 到 7 之间绕:

| 逻辑序号 | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 物理下标 | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |

活着的元素永远是逻辑序号 `[read_pos, write_pos)` 对应的那些格子,它可能整段躺在数组中间,也可能从尾巴绕回头部。Chromium 只用一个 `current_index_`,因为人家只写不删,一条写指针就够。咱们要 pop,读写两条都得带上。

## 指针只增不减,584 年才回绕

您可能想问,序号一直加,不会溢出吗。咱们动手算一遍就放心了。两个序号都用 `std::uint64_t`,2 的 64 次方是 18,446,744,073,709,551,616,约 1.84 × 10^19。就算拿它当高频日志的计数器,每秒写十亿次,序号走满 2 的 64 次方也要 1.84 × 10^10 秒。一年按 365.25 天算是 31,557,600 秒,两个数一除,约 584 年。任何软件都活不到这个回绕点,所以序号可以放心地只增不减。Chromium 的 `current_index_` 是 `size_t`,在 64 位平台上同样 8 字节,同一个设计。

只增不减换来的便利,咱们下一节马上兑现。

## 判空判满,教科书三个解法

环形的经典难题:转了一圈回到原点,`read_pos` 和 `write_pos` 相等,这时候是空还是满?两种状态长一个样。教科书上有三个解法,咱们挨个过。

头一个叫牺牲一格,永远留一格不用,`write_pos` 追到 `read_pos` 旁边一格就算满。它的代价咱们直接付在容量上,凭空少一格,取模之后的比较还散落在各个操作里。

再一个是计数器,单独维护一个 `size_` 成员,直白好懂。代价是每个操作多一次读写,咱们还得多背一份要时刻保持同步的状态,忘同步一次,空满就全乱了。

最后一个就是咱们上一节的做法,单调索引。序号 64 位只增不减,空是 `read_pos == write_pos`,满是 `write_pos - read_pos == N`,一次整数减法出结果,额外状态一个都不用。主线咱们取它,它和 584 年不回绕是同一个决定的两面。牺牲一格并不是不好,那套手艺留给下一站的 CircularDeque,那边的解环搬迁跟它更配,到时候对照着看,您会有感觉。

## 2 的幂:取模写成位与

当 N 是 2 的幂,x % N 与 x & (N - 1) 对非负的 x 恒等。道理不深:N = 8 的二进制是 1000,N - 1 是 0111,拿它做位与,留下的恰好是 x 的低三位,而低三位正是 x 除以 8 的余数。咱们的序号永远非负,前提天然成立。于是 RingBuffer 把 N 定成模板参数,还在类里拦了一道:

```cpp
template <typename Sources, std::size_t N> class RingBuffer {
    // N 不是 2 的幂,& (N - 1) 的等价性就没了,拦在编译期
    static_assert(N != 0 && (N & (N - 1)) == 0, "N must be a power of two");
```

您可能想问,编译器看见 `x % 8` 这样的常量取模,难道不会自己优化吗?会,而且咱们马上用汇编眼见为实。那为什么还手写 `& (N - 1)`?手写把依赖写在明面上:读代码的人一眼看出这里的等价性依赖 N 是 2 的幂,哪天想放宽约束,这一处跑不掉。Chromium 的选择正相反,`ring_buffer.h` 对 kSize 没有任何 2 的幂要求,人家直接写 `% kSize`,优化全交给编译器。两种立场都成立,咱们把理由摆出来,您站哪边都行。

## 汇编小实验,三种取模的下场

口头说编译器会优化不算数,咱们写三个小函数,N 的来历各不相同,拿本机 g++ 16.2.1 跑 `-O2 -S`,看看每个取模变成了什么。类型用 `std::uint64_t`,和序号的真实类型一致:

```cpp
#include <cstdint>

// 三种取模,对应环形缓冲里 N 的三种来历
std::uint64_t mod_pow2(std::uint64_t x) {
    return x % 8; // 编译期常量,2 的幂
}
std::uint64_t mod_other(std::uint64_t x) {
    return x % 6; // 编译期常量,不是 2 的幂
}
std::uint64_t mod_runtime(std::uint64_t x, std::uint64_t n) {
    return x % n; // 运行时变量
}
```

`mod_pow2` 的函数体只剩一条位与,和咱们手写的 `& 7` 一个待遇:

```asm
movq	%rdi, %rax
andl	$7, %eax
ret
```

`mod_other` 的场面就复杂了:

```asm
movabsq	$-6148914691236517205, %rax
mulq	%rdi
movq	%rdx, %rax
shrq	$2, %rax
leaq	(%rax,%rax,2), %rdx
movq	%rdi, %rax
addq	%rdx, %rdx
subq	%rdx, %rax
ret
```

咱们把指令逐条翻一下:编译器拿魔法数 -6148914691236517205 做宽乘取高位,再右移两位,得到 x / 6,最后用 x 减去 6 × (x / 6) 拿到余数。这就是除法的强度削减,比真除法便宜,比一条位与贵得多。最后是运行时变量,咱们看到的是货真价实的除法指令:

```asm
movq	%rdi, %rax
xorl	%edx, %edx
divq	%rsi
movq	%rdx, %rax
ret
```

64 位除法在 x86-64 上是出了名的慢,Agner Fog 的指令表里,Skylake 一代 64 位 div 的延迟要几十个周期起步,位与只要一个周期上下。三种下场摆在眼前,咱们的处境其实最舒服:N 是模板参数,常量的特权全占,写 `% N` 和写 `& (N - 1)` 生成的都是同一条 and。手写位与的意义剩下一条,把约定写进代码,给读的人看。

## 单文件版 RingBuffer,一条命令编出来

stage2 的工程版按系列惯例由作者亲手搓,施工讲义已经躺在配套目录里。文章这边咱们把教学版做成自洽的单文件,下面三段代码按顺序拼起来,就是完整的 `ring_buffer_demo.cpp`。头部是 `Check`,stage1 的崩溃式检查原样搬来:

```cpp
// ring_buffer_demo.cpp - RingBuffer 教学单文件版
// 编译:g++ -std=c++20 ring_buffer_demo.cpp -o ring_buffer_demo
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <source_location>
#include <utility>

namespace tamcpp::ministl {
namespace debug {
// 崩溃式检查,stage1 的 check.hpp 原样搬来
static inline void Check(bool IsExpectedTrue, const char* msg,
                         std::source_location loc = std::source_location::current()) {
    if (IsExpectedTrue) {
        return; // 哈,就是对的?无事发生~
    }
    std::fprintf(stderr, "[TAMCPP Check Crash]%s-%d(%s): %s\n", loc.file_name(), loc.line(),
                 loc.function_name(), msg);
    std::abort(); // 再见了小朋友
}
} // namespace tamcpp::ministl::debug
```

主角全貌如下,查询、push、pop、front、visit_at、clear 一件不缺:

```cpp
namespace tamcpp::ministl {

template <typename Sources, std::size_t N> class RingBuffer {
    // N 不是 2 的幂,& (N - 1) 的等价性就没了,拦在编译期
    static_assert(N != 0 && (N & (N - 1)) == 0, "N must be a power of two");

  public:
    RingBuffer() : ring_(static_cast<Sources*>(::operator new(N * sizeof(Sources)))) {}

    // 教学版只留移动这一件,五件套的完整版进 stage2 工程再补
    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;
    RingBuffer(RingBuffer&& other) noexcept
        : ring_(other.ring_), read_pos_(other.read_pos_), write_pos_(other.write_pos_) {
        other.ring_ = nullptr;
        other.read_pos_ = other.write_pos_ = 0;
    }

    ~RingBuffer() {
        // 活元素绕了一圈,物理上不保证连续,沿逻辑序号走才收得干净
        for (std::uint64_t i = read_pos_; i != write_pos_; ++i) {
            ring_[i & (N - 1)].~Sources();
        }
        ::operator delete(ring_);
    }

    [[nodiscard]] bool empty() const { return read_pos_ == write_pos_; }
    [[nodiscard]] bool full() const { return write_pos_ - read_pos_ == N; }
    [[nodiscard]] std::size_t size() const {
        return static_cast<std::size_t>(write_pos_ - read_pos_);
    }

    void push(Sources v) {
        if (full()) {
            // 满了:最老的让位,格子腾给最新的
            ring_[read_pos_ & (N - 1)].~Sources();
            ++read_pos_;
        }
        std::construct_at(ring_ + (write_pos_ & (N - 1)), std::move(v));
        ++write_pos_; // 序号只增不减,绕圈全交给 & (N - 1)
    }

    void pop() {
        debug::Check(!empty(), "pop on empty RingBuffer");
        ring_[read_pos_ & (N - 1)].~Sources();
        ++read_pos_;
    }

    Sources& front() {
        debug::Check(!empty(), "front on empty RingBuffer");
        return ring_[read_pos_ & (N - 1)];
    }
    const Sources& front() const {
        debug::Check(!empty(), "front on empty RingBuffer");
        return ring_[read_pos_ & (N - 1)];
    }

    Sources& visit_at(std::size_t logical) {
        // 界是 size 不是 N:空格子里没有活着的对象
        debug::Check(logical < size(), "visit_at out of range");
        return ring_[(read_pos_ + logical) & (N - 1)];
    }
    const Sources& visit_at(std::size_t logical) const {
        debug::Check(logical < size(), "visit_at out of range");
        return ring_[(read_pos_ + logical) & (N - 1)];
    }

    void clear() {
        for (std::uint64_t i = read_pos_; i != write_pos_; ++i) {
            ring_[i & (N - 1)].~Sources();
        }
        read_pos_ = write_pos_ = 0;
    }

  private:
    Sources* ring_;
    std::uint64_t read_pos_{0};  // 读到哪个逻辑序号
    std::uint64_t write_pos_{0}; // 写到哪个逻辑序号,单调递增
};
} // namespace tamcpp::ministl
```

内存照 stage1 的路子,`::operator new` 圈 N 格,只管容量不管对象。查询三件全是减法和比较,额外状态一点没存。push 承载着覆盖语义:缓冲没满时,构造、推进,和 Vector 的 push_back 一样轻。满了的时候,最老的元素就站在 read_pos 指到的格子里,咱们析构它,把 read_pos 推一格,腾出来的位置正好接新的。顺序是析构最老、推进读指针、构造新的、推进写指针,一步都不能乱,乱了 Census 的计数就配不平,验收那节见分晓。异常安全顺带说一句:构造排在推进前面,非满时构造要是抛了,序号还站在原地,缓冲纹丝不动。满了时最老的已经让位,新的构造要是失败,它就真的丢了,这是覆盖式接受的代价。

pop 的返回类型是 void,出队想要值,得另走 front。这个别扭是故意的。假如 pop 按值返回元素,返回时的拷贝构造一旦抛出,元素已经从缓冲里删了,数据就这么丢了。拆成 front 加 pop,取值和出队各走各的,哪一步都出得起异常。标准库的 `std::queue::pop` 同款返回 void,您可以在 cppreference 核对签名。

pop 空缓冲那行 Check,遵循系列导论立的分工:照谁的镜子,错误处理就跟谁。RingBuffer 照的是 Chromium,前置条件违反就当场崩,不抛异常给调用方接。std 家的 `std::vector::at` 是另一队,越界抛 `std::out_of_range`,调用方接得住、能恢复。front 空、visit_at 越界,在咱们这边全是 Check 的统一待遇。visit_at 的界也有讲究,是 size 不是 N,空着的格子里对象根本没出生,访问它们是未定义行为,Check 得拦在前面。

## 覆盖与绕回,Census 验收

第三段代码是计数类型和 main,拼上它整个文件就齐了:

```cpp
// 出生 +1、死亡 -1 的计数类型,01 篇的 Census
struct Census {
    inline static int alive = 0;
    int tag;
    explicit Census(int t) : tag(t) { ++alive; }
    Census(const Census& other) : tag(other.tag) { ++alive; }
    Census(Census&& other) noexcept : tag(other.tag) { ++alive; }
    ~Census() { --alive; }
};

int main() {
    using tamcpp::ministl::RingBuffer;

    RingBuffer<int, 8> rb;
    for (int i = 1; i <= 13; ++i) {
        rb.push(i); // 1..5 进来就被后面挤掉了
    }
    std::printf("size=%zu front=%d (期望 6)\n", rb.size(), rb.front());
    std::printf("出队顺序:");
    while (!rb.empty()) {
        std::printf(" %d", rb.front());
        rb.pop();
    }
    std::printf("\n");

    {
        RingBuffer<Census, 8> c;
        for (int i = 0; i < 13; ++i) {
            c.push(Census{i});
        }
        std::printf("压入 13 个 Census,活着 %d 个(期望 8)\n", Census::alive);
        for (int i = 0; i < 3; ++i) {
            c.pop();
        }
        std::printf("出队 3 个,活着 %d 个(期望 5),front 的 tag=%d(期望 8)\n", Census::alive,
                    c.front().tag);
        std::printf("逻辑序号 3 上的 tag=%d(期望 11)\n", c.visit_at(3).tag);
    }
    std::printf("作用域结束,活着 %d 个(期望 0)\n", Census::alive);
    return 0;
}
```

第一轮拿 int 验顺序,压 1 到 13 再一口气出队,真实输出:

```text
size=8 front=6 (期望 6)
出队顺序: 6 7 8 9 10 11 12 13
```

13 个进来,活下来的是 6 到 13,先进先出的序一点没乱,front 正是第 6 个进来的。第二轮的 Census 输出:

```text
压入 13 个 Census,活着 8 个(期望 8)
出队 3 个,活着 5 个(期望 5),front 的 tag=8(期望 8)
逻辑序号 3 上的 tag=11(期望 11)
作用域结束,活着 0 个(期望 0)
```

覆盖时被挤掉的 5 个、出队的 3 个、析构掉的 8 个,每次死亡都有对应的出生,数字全部对得上。tag 那两行顺带验证了访问:pop 过 3 个之后,front 是 tag 为 8 的元素,也就是第 9 个进来的,`visit_at(3)` 从它再往后数三个。

## 析构绕圈,连续区间的假设失效

stage1 留下的 `DestroySources(begin, end)` 有个隐含假设,活元素是连续一段。Vector 的世界里这永远成立,环形的世界里不成立:活元素从数组尾巴绕回头部,物理上是两段。咱们做个独立的小实验,模拟压入 10 个、出队 7 个之后的状态,read_pos = 7,write_pos = 10,活元素散在物理 7、0、1 三格:

```cpp
// 错误示范:把 [0, size) 当连续区间交出去
// 它走到的是物理 0、1、2 三格:2 号格从未出生,7 号格不在区间里
DestroySources(ring, ring + static_cast<std::size_t>(write_pos - read_pos));
std::printf("连续区间版走完,活着 %d 个(数字碰巧对了)\n", Census::alive);

ring[7].~Census(); // 7 号格里的对象还活着,补杀一次,真相现形
std::printf("补杀 7 号格,活着 %d 个\n", Census::alive);
```

```text
活着 3 个,物理位置在 7、0、1 三格
连续区间版走完,活着 0 个(数字碰巧对了)
补杀 7 号格,活着 -1 个:绕回的半圈被漏掉了
```

这段输出值得咱们多看两眼。报数 0 看起来配平了,来路却全错:DestroySources 走过物理 0、1、2 三格,0 和 1 里的对象死了,2 号格里从来没有对象出生,对未构造的内存调析构是未定义行为。7 号格里的活对象,它压根没碰。一个漏杀配一个错杀,计数碰巧抵消成 0。Census 只是玩计数器的平凡类型,这次错误析构没有当场发作,可要是元素换成持有资源的类型,漏掉的那半圈就是泄漏,错杀的格子就是重复释放。补杀 7 号格,计数掉到 -1,被漏掉的部分这才现形。

正确版的写法就在上面的析构函数里,沿逻辑序号从 read_pos 绕到 write_pos,一圈走完,每个活元素恰好轮到一次。拷贝遵循同一个思路,您补五件套的时候留意:按逻辑序号从最老复制到最新,新缓冲的 read_pos 从 0 起步。直接 memcpy 物理内存会把死格子一起抄走,对方的绕圈状态也会原样搬过来,两个对象的指针对不上。移动那一对更省心,指针和两个序号接管过来、对面清零,标上 noexcept,为什么标,03 篇的 move_if_noexcept 实测就是完整答案,咱们不展开。

## 死法用例与编译期拦截

死法用例照 stage1 的老办法:pop 一个空缓冲,正确结局是当场崩。把 main 换成三行,重新编译运行,真实输出:

```text
[TAMCPP Check Crash]ring_buffer_death.cpp-66(void tamcpp::ministl::RingBuffer<Sources, N>::pop() [with Sources = int; long unsigned int N = 8]): pop on empty RingBuffer
```

进程以 SIGABRT 收场,退出码 134。要是程序还能打印“运行到这了”,检查就是假的。ctest 见到信号打死的子进程一律判负,所以 stage1 那会儿咱们垫了裁判脚本 `expect_check_death.sh`,工程版沿用同一套流程就好。

编译期的拦截也实测给您看,N 给成 6:

```text
error: static assertion failed: N must be a power of two
   27 |     static_assert(N != 0 && (N & (N - 1)) == 0, "N must be a power of two");
      |                             ~~~~~~~~~~~~~~^~~~
  • the comparison reduces to '(4 == 0)'
```

GCC 连 N 取 6 时 `N & (N - 1)` 算出 4 都替您展示出来了。编译期的拦截比运行时崩溃更早,错误连进程都起不来。

到这里,单文件版的 RingBuffer 能进能出、能覆盖能绕圈,验收数字全部对得上。工程版落进 `stage2_ring_buffer/` 之后,五件套补齐,黑盒测试再把这一切独立地重来一遍。下一篇的 CircularDeque 要可变容量、要迭代器,牺牲一格的判满方案在那边等咱们,解环搬迁的手艺也一并开工。

## 构建与复现

```bash
# 单文件教学版,本篇三段代码按顺序拼成 ring_buffer_demo.cpp
g++ -std=c++20 ring_buffer_demo.cpp -o ring_buffer_demo
./ring_buffer_demo                          # FIFO 绕回 + Census 配平
# 死法用例:main 换成三行重新编译
g++ -std=c++20 ring_buffer_death.cpp -o ring_buffer_death
./ring_buffer_death                         # 期望 Check 崩溃,退出码 134
# 析构绕圈实验
g++ -std=c++20 destroy_wrap_demo.cpp -o destroy_wrap_demo && ./destroy_wrap_demo
# 汇编实验
g++ -std=c++20 -O2 -S mod.cpp -o mod.s      # 三种取模的机器码都在 mod.s 里
```

配套工程目录 `code/volumn_codes/vol8-labs/ministl/stage2_ring_buffer/`,施工讲义 `HANDBOOK.md` 已就位,工程版头文件 `include/tamcpp_ministl/ring_buffer.hpp` 由作者按讲义推进。

## 参考资源

- 配套代码:`code/volumn_codes/vol8-labs/ministl/stage2_ring_buffer/`
- [Chromium `base/containers/ring_buffer.h`](https://chromium.googlesource.com/chromium/src/+/main/base/containers/ring_buffer.h)(本篇镜子,139 行,注释原文在 `IsFilledIndexByBufferIndex` 上方)
- [cppreference:`std::queue::pop`](https://en.cppreference.com/w/cpp/container/queue/pop)(返回 void 的同款决定)
- [Agner Fog 指令表](https://www.agner.org/optimize/instruction_tables.pdf)(除法延迟的出处,[uops.info](https://uops.info/) 可交叉核对)
- [mini STL 实战(一):RawBuffer,只管容量,不管对象](01-raw-buffer.md)(`Check` 与 `DestroySources` 的出身)
- [mini STL 实战(三):Vector,五件套、异常安全与 concepts](03-vector-copy-move-and-concepts.md)(noexcept 与五件套的完整讨论)
