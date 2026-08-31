---
title: "mini STL 实战(三):Vector,五件套、异常安全与 concepts"
description: "补齐 Vector 的拷贝与移动:深拷贝、copy-and-swap 三步、move-and-swap 的先偷再换、noexcept 换来的零拷贝实证,以及 resize 的 concepts 约束。中间复盘一个笔者亲踩的 bug:swap 写成了偷,前三轮测试全都没发现问题,UBSan 才把它揪出来。"
chapter: 3
order: 3
tags:
  - host
  - cpp-modern
  - intermediate
  - 容器
  - vector
  - 移动语义
  - concepts
difficulty: intermediate
platform: host
reading_time_minutes: 13
cpp_standard: [17, 20, 23]
prerequisites:
  - "mini STL 实战(二):Vector,扩容与搬迁"
related:
  - "mini STL 实战(四):RingBuffer,最短的环形入门"
---

# mini STL 实战(三):Vector,五件套、异常安全与 concepts

在咱们开始之前，先来看一个数,本篇结尾解释它从哪来:

```text
outer=20 个,元素活着 100 个,元素级拷贝 0 次
```

20 个各装 5 个元素的 `Vector` 搬进外层 `Vector`,外层中途扩容了好几轮,元素级的拷贝构造发生了零次。这个 0,是咱们五件套里两个 `noexcept` 挣来的。上一篇结尾 Vector 还挂着 `DISABLE_COPY`,这一篇把它们逐一写全:析构、拷贝构造、拷贝赋值、移动构造、移动赋值。

## swap

```cpp
    void swap(Vector& other) noexcept {
        std::swap(buffer_, other.buffer_);
        std::swap(current_cnt_, other.current_cnt_);
    }
```

`std::swap` 内部是三步移动:造一个临时,内容互倒,最后临时析构。它能直接用在 `RawBuffer` 上,靠的就是第一篇写的那个移动赋值——先释放自己,再接管对方,把对方置空。您可能想问:第一步释放自己的时候,万一手里的指针是空的呢?没事,释放空指针是标准明文允许的空操作。有这条保底,三步走下来才严丝合缝。后面要写的拷贝赋值和移动赋值,底座都是这四行。

这个函数要是写错一个字,用到它的地方全部返工。笔者自己就在它上面摔过一跤,后面专门拿一节复盘那次事故。

## 拷贝构造与拷贝赋值

```cpp
    Vector(const Vector& other) {
        // 是 capacity 还是走默认大小看情况,标准库是已经使用的大小,我们对齐
        reserve(other.current_cnt_);
        for (size_t i = 0; i < other.current_cnt_; ++i) {
            std::construct_at(buffer_.data() + i, other.buffer_.data()[i]);
        }
        current_cnt_ = other.current_cnt_;
    }
    Vector& operator=(const Vector& other) {
        Vector other_{other};
        swap(other_);
        return *this;
    }
```

咱们先看拷贝构造,它做的事情就是深拷贝:另外申请一块内存,把元素一个一个拷贝构造过去。浅拷贝(只抄指针)的结局是两个 Vector 共一块内存,一个析构,另一个手里就是悬垂指针。申请多大的内存有取舍:按已用大小开,对方富余的空位不跟,和标准库对齐。

拷贝赋值走 copy-and-swap,分三步。先用拷贝构造造一个临时 `other_`——注意,这一步动的是新内存,`*this` 还一个字没改。然后把内容和它对调。最后函数返回,临时析构,旧数据跟着它一起下线。这个写法好在哪?咱们得看它的对手怎么死。直觉写法是"先杀旧数据、再拷新数据",它有两个死法。第一个是自我赋值。现实里自我赋值一般藏在别名后面:两个引用指着同一个对象,函数里一行 `a = b`,没人保证它们不是同一个。真碰上了,先杀旧数据,再去"对方"里拷,拷到的是自己刚杀掉的尸体。第二个是异常:拷贝构造中途因内存不足抛了异常,自己的数据已经销毁,对象卡在半死状态,想恢复都没得恢复。copy-and-swap 对这两个都免疫。碰上自我赋值?大不了白拷一份再换回来,数据一点不伤。碰上拷贝抛异常?异常发生在动 `*this` 之前,自己毫发无伤。到最后连 `if (this != &other)` 的特判都省了,反正这里确实用不着。

咱们在测试里专门验证过这种情况,用的就是别名:`Vector<int>& alias = assigned; assigned = alias;`。绕这么一下,是为了躲开 clangd 对故意自赋值的唠叨。自赋值必须安全,但不必大张旗鼓。

## 一个"偷"字的 bug

现在复盘。笔者写过另一版实现,`swap` 长这样:

```cpp
    void swap(Vector& other) noexcept {
        buffer_ = std::move(other.buffer_);   // ← 把对方的抢过来,自己的旧地已释放
        std::swap(current_cnt_, other.current_cnt_);
    }
```

咱们把这句放大看:`buffer_ = std::move(other.buffer_)` 调的是 `RawBuffer` 的移动**赋值**,把自己那块释放,接管对方的,对方置空。净效果:对方那块归了自己,自己原先那块被释放。拷贝赋值 `a = b` 于是变成:临时拷贝出 b 的内容,a 抢走临时那块内存,临时拿着空指针和一个没归零的计数去析构:对着 `(nullptr, 旧size)` 逐个调析构函数,这就是一个未定义行为。

这个 bug 在之前，连续三轮的测试里都没露馅。走赋值的路径的元素全是 `int`,平凡可析构,析构循环在编译期就被裁掉了,空指针根本没被碰。到第四轮咱们换上非平凡类型,计数一加一减恰好抵消,断言照样全过。直到一轮例行的探测器回归,UBSan 打出:

```text
include/.../raw_buffer.hpp:46:22: runtime error:
member call on null pointer of type 'struct Census'
```

栈回溯拉出来,三分钟就定位了。这个坑的教训值得咱们记牢。测一个管资源的容器,非平凡类型一定要打到赋值路径上——平凡类型会在沉默里放行这类 bug,您根本察觉不到。还有 swap 的契约本身:它承诺的是"换",而移动赋值承诺的是"接管对方的,释放自己的"。两个契约就差一个字,可 copy-and-swap 建立在"换"字上的整套安全论证,到这里就不成立了。

## 移动构造与移动赋值

```cpp
    // noexcept 是保证咱们搬动内存的时候不出事情:move_if_noexcept!
    Vector(Vector&& other) noexcept
        : buffer_(std::move(other.buffer_)), current_cnt_(other.current_cnt_) {
        other.current_cnt_ = 0;
    }
    Vector& operator=(Vector&& other) noexcept {
        // 照抄 trick:move and swap —— 先把对方偷到手,再换身份;
        Vector looter(std::move(other));
        swap(looter);
        return *this;
    }
```

移动构造干的事很朴素,您看:把对方的内存接管过来,计数抄过来,再把对方清零。被移动的一方(moved-from)拿到的是确定语义:空数组。标准对 std 容器只承诺"有效但未指定",教学库可以把话说满:移动后的对象就是空数组。测试里验过:移动后的 `donor` 是空的,还能接着 `push_back(42)` 用。

咱们的移动赋值是 move-and-swap:先把对方偷到手(经过移动构造,对方成了确定的空对象),再和 `looter` 换身份,旧数据随 `looter` 析构。自移动也顺带免疫:`a = std::move(a)`,a 先被偷空,再从 `looter` 里换回来,完璧归赵,连判 `this` 都不用。比 copy-and-swap 的对偶版多走一次移动构造,对 Vector 这种"两个指针加一个计数"的类型,这点代价可以忽略。

## noexcept 换来的零拷贝

本篇开头那个"0 次",现在可以解释了。场景是这样的:咱们有一个数组的数组,外层是 `Vector<Vector<Census>>`;外层扩容的时候,里面的内层 Vector 也得跟着搬。搬一个内层有两种搬法。用移动,只要把指针倒一下手,非常便宜;用拷贝,得把它里面的元素全部重新复制一遍。谁都会选移动,标准库也想选——但它多想了一层:万一搬到一半,某个元素的移动构造抛了异常呢?这时候新数组已经搬了一半,旧数组也已经被破坏,想退,退不回去了。拷贝构造就没这个麻烦,它抛异常时旧数组还原封没动,回滚就是。所以 `std::vector` 扩容走的是 `std::move_if_noexcept`:您的移动构造带上 `noexcept`、承诺绝不抛异常,它才敢用移动;不然宁可慢一点,也要拷贝。那个函数名字拗口,拗的就是这个取舍。

咱们的移动构造和移动赋值都标了 `noexcept`,咱们还用测试文件开头的两条 `static_assert`,把这个承诺写进了编译期。接着咱们做个实验,例程在 `example/vector_move_if_noexcept.cpp`:给计数类型再加一个只统计拷贝构造的计数器,把 20 个各装 5 个元素的 Vector 移动入库,让外层扩容好几轮,最后看这个计数器停在几:

```text
outer=20 个,元素活着 100 个,元素级拷贝 0 次
```

从入库到历次扩容,元素级拷贝一次都没发生。您要是有兴趣,可以做个对照:把移动构造的 `noexcept` 临时删掉再跑一遍,那个 0 就不再是 0,扩容改走拷贝了。一个关键字,决定扩容时整套搬迁走哪条路。

## resize 与 concepts

```cpp
    void resize(std::size_t new_size)
        requires(std::default_initializable<Sources>)
    {
```

resize 干的事很直白:变小,就把尾巴上多出来的元素析构掉;变大,就在后面用默认构造补出新的格子。不过变大这一路有个前提,`Sources` 得能默认构造才行。这个前提咱们直接写在函数签名上,谁来调用,第一眼就看得见。老一点的写法是把检查藏进函数体里,等到模板真正实例化的时候才炸出一屏错误,看得人头皮发麻。咱们试一下,拿一个没有默认构造的类型去调它,看看 GCC 报什么:

```text
error: no matching function for call to
'tamcpp::ministl::Vector<main()::NoDefault>::resize(int)'
  • candidate 1: 'void tamcpp::ministl::Vector<Sources>::resize(std::size_t)
                  requires  default_initializable<Sources> [with Sources = ...]'
      • constraints not satisfied
```

哪个候选、卡在哪条约束、为什么,报错一行行说得清清楚楚。约束成了接口文档的一部分,违约的报错报在人脸上,这就是 concepts 相对 SFINAE 给您的体感差距。例程 `example/vector_resize_concepts.cpp` 里留着注释掉的三行,您打开它们,就能亲眼看到这场拦截。

## 验收

咱们在 `tests/test_vector.cpp` 的后半段,把本篇的内容各验一遍:swap 对换、深拷贝(拷贝完改副本,原件不许动)、copy-and-swap 连同别名自赋值、移动后的对象可复用、move-and-swap 连同自移动、visit_at 界内读写、resize 的四种情形。这些测试全部通过:

```text
$ ./build/tests/test_vector
VECTOR ALL GREEN
```

到这里,咱们的 `Vector` 五件套齐装,`stage1_rawbuf_vector` 收官。下一篇咱们离开连续内存进环形:下标算术第一次要绕圈,而 `% N` 这一步的取舍,Chromium 那边连"多取一次模都嫌贵"的注释都写好了。

## 构建与复现

```bash
cd code/volumn_codes/vol8-labs/ministl/stage1_rawbuf_vector
cmake -B build . && cmake --build build
(cd build && ctest --output-on-failure)          # 4/4
./build/example/vector_move_if_noexcept          # 零拷贝实证
./build/example/vector_resize_concepts           # concepts 拦截演示
```

## 参考资源

- 配套代码:`code/volumn_codes/vol8-labs/ministl/stage1_rawbuf_vector/`
- [cppreference:`std::move_if_noexcept`](https://en.cppreference.com/w/cpp/utility/move_if_noexcept)
- [移动语义实战:从 STL 到自定义类型](../../vol2-modern-features/ch00-move-semantics/05-move-in-practice.md)(vol2 语言特性层,本篇实现层)
- [综合项目:concepts 约束的 mini-STL 算法库](../../vol4-advanced/vol3-metaprogramming-cpp20-23/09-mini-stl-with-concepts.md)(vol4,concepts 的另一面)
