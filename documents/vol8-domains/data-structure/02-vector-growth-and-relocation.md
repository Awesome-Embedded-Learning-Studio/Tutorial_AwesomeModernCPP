---
title: "mini STL 实战(二):Vector,扩容与搬迁"
description: "在 RawBuffer 上把动态数组立起来:两个成员、emplace_back 的参数转发、翻倍扩容与摊还分析。容量轨迹真跑给您看,搬迁零新增代码。地基的 Relocate 原班人马直接复用。"
chapter: 2
order: 2
tags:
  - host
  - cpp-modern
  - intermediate
  - 容器
  - vector
  - 内存管理
difficulty: intermediate
platform: host
reading_time_minutes: 8
cpp_standard: [17, 20, 23]
prerequisites:
  - "mini STL 实战(一):RawBuffer,只管容量,不管对象"
related:
  - "mini STL 实战(三):Vector,五件套、异常安全与 concepts"
---

# mini STL 实战(二):Vector,扩容与搬迁

咱们往 `tamcpp::ministl::Vector` 里连塞 40 个数,每次容量变化打一行(配套例程 `example/vector_capacity_trace.cpp`):

```text
初始         size=0   capacity=0
第 1  个入库 size=1   capacity=4(扩容)
第 5  个入库 size=5   capacity=8(扩容)
第 9  个入库 size=9   capacity=16(扩容)
第 17 个入库 size=17  capacity=32(扩容)
第 33 个入库 size=33  capacity=64(扩容)
最终         size=40  capacity=64
```

40 次入库,只有 5 次扩容,容量一路翻倍。这一篇咱们就把这个轨迹解释清楚,顺便把动态数组整个搓出来。上一篇的 `Relocate` 在扩容那一步原样复用,一行不改。

## 成员:一个指针带一个计数

```cpp
  private:
    RawBuffer<Sources> buffer_;
    std::size_t current_cnt_{0};
```

`buffer_` 是那块裸内存,`current_cnt_` 数着活元素。内存里前 `current_cnt_` 格是活对象,后面是从未构造过的空位。libstdc++ 的 `std::vector` 画的也是这张图,只是换了个画法。它存三个指针,放在 `_Vector_impl_data` 里(`bits/stl_vector.h:98-102`):起点、尾后、仓底。想知道 size,拿前两个一减;想知道容量,拿后两个一减。咱们实测一下尺寸,两版一样:

```text
sizeof(tamcpp::ministl::Vector<int>) = 24
sizeof(std::vector<int>)             = 24
```

## 出生与死亡

构造函数有三个。默认构造什么都不做。`RawBuffer` 生下来就是空指针加零容量,这恰好是一个合法的空数组。花括号构造就是遍历一遍初始化列表,把元素一个一个往里追加。还有一个容量构造,是咱们工程自己的扩展:`Vector<int> pre(8)` 只申请内存、不构造对象。`std::vector` 没有这种构造,但库内部和测试都用得上它。

```cpp
    Vector(std::initializer_list<Sources> init_lists_src) {
        const auto sz = init_lists_src.size();
        reserve(sz);
        for (const Sources& v : init_lists_src) {
            emplace_back(v); // 往里进!往里进!
        }
    }
```

析构一行,却是最不能写错的一行:`helper::DestroySources(buffer_.data(), buffer_.data() + current_cnt_)`。

兄弟们，笔者自己写，是真的老容易出错，每一次都是掰手指算。多杀一格是对未构造内存调析构,少杀一格是泄漏。内存本身的归还在 `RawBuffer` 析构里,RAII 给咱们兜底,容器算法层只管对象。

## emplace_back:就地出生

```cpp
    template <typename... Args> Sources& emplace_back(Args&&... args) {
        if (current_cnt_ == buffer_.capacity()) {
            /* 为什么给4,实际上是随意给的,我们这里没有做严肃的profile */
            grow_to(std::max<size_t>(4, buffer_.capacity() * 2));
        }

        Sources* p = std::construct_at(buffer_.data() + current_cnt_, std::forward<Args>(args)...);
        ++current_cnt_;
        return *p;
    }
```

您给什么构造参数,它就原样转发给 `Sources` 的构造函数。元素直接在数组尾巴上出生,不经过临时对象。举个最直观的例子:`std::string` 的 `emplace_back(5, 'x')` 能直接造出 `"xxxxx"`。这套转发靠的是 `Args&&...` 加 `std::forward`。左值进来,按左值转;右值进来,按右值转,移动的机会不会在半路被吞掉。

构造点咱们用的是 `std::construct_at`。它是 C++20 给 placement new 发的官方马甲,内部干同一件事,多了 constexpr 资格。两样都值得认识:Chromium 的代码里满篇本尊,新代码用马甲。

满了就扩容。咱们给的起始容量是 4,这个数没做过正经的性能测试,拍脑袋定的;真正有讲究的是"每次翻倍",下面把代价算清楚。

## grow_to 与摊还分析

```cpp
    void grow_to(std::size_t new_cap) {
        RawBuffer<Sources> new_buf_(new_cap);
        helper::Relocate(buffer_.data(), buffer_.data() + current_cnt_, new_buf_.data());
        buffer_ = std::move(new_buf_);
    }
```

扩容就这么几步:咱们先申请一块新的内存,把活着的元素搬过去,旧缓冲交给移动赋值去释放。搬迁用的还是上一篇那个 `Relocate`,一行没改。平凡类型整块 `memcpy` 搬走;其余的,逐个移动构造过去。那老位置上的元素谁来收拾?不用收拾——搬运途中,它们就已经逐个析构掉了,搬完不需要再补一轮。整个流程里也没有"先拷到新家、再杀旧对象"这种双倍开销,从头到尾走的都是移动。

`push_back` 平时只是在一格空位上构造一个对象,常数时间;可容量一旦满了,那一次就是全量搬迁,线性的代价。那凭什么还说 `push_back` 是摊还常数?咱们把总代价算一算就明白了。容量从 4 开始翻倍,所以第 k 次扩容要搬 2^k 个元素;一路塞到 n 个,总的搬迁代价是 4 + 8 + … + n。这是标准的等比级数,加起来不到 2n。摊回每次 push 上,不足两次元素搬运——开头那份容量轨迹,40 次入库只有 5 次扩容,就是这组数字的样子。

摊还成立还有个前提:"新容量与旧容量成固定比例",这一点您先记下,环形篇还要回来用。翻倍可以,1.5 倍也可以,MSVC 用的就是 1.5。但每次只加一格不行:那样 n 次 push 的总搬迁是 1+2+…+n,平方级的。Chromium 的 `circular_deque` 选了只加 25%,注释给的理由是队列负载平稳、渐进增长省内存(`circular_deque.h:1112-1114`)——那边的场景和数组不同,轮到环形篇再说。

`reserve(n)` 派的就是这个用场:咱们提前知道要装多少,一次申请到位,中间不搬。它只涨不缩,测试里有专门一条:`r.reserve(10)` 之后再 `reserve(5)`,容量还是 10。

## 访问、迭代与收尾

访问这块,连续存储的好处最先体现出来。`begin()` 和 `end()` 返回的就是裸指针,而连续内存上的裸指针,天生就是合格的迭代器:自增、比较、解引用,样样都行,`std::sort` 和范围 for 拿来就用。等链表篇咱们为同一套接口写一整个迭代器类的时候,回头再看这几行,就知道它们替咱们省了什么。

下标访问,咱们做成双轨。`operator[]` 不做检查,信任调用方;`visit_at` 带 `Check`,但检查的界是 `current_cnt_`,capacity 再大也不算数。这个界专门有死亡测试盯着:故意 reserve(8) 只装 3 个元素,然后去访问第 5 格——把界错查成 capacity 的实现,过不了那一关。

`pop_back` 的顺序有讲究,咱们先减计数,再杀最后一格。写反了杀的是倒数第二格,最后一格还白白占着。`clear` 杀光活元素、容量保持,和 `std::vector` 一致:清空只杀对象,内存还留着。

## 验收

搬迁有没有搬错,咱们靠下标查。从 {1,2,3,4} 起手,再追加 100 个,中途扩容四次;然后断言 `v[50]` 必须等于 46。搬错一格,这个断言立刻把它暴露出来。

构造和析构有没有配对,咱们请第一篇那个"出生 +1、死亡 -1"的计数类型来盯。加一个元素、删一个元素,活着的对象数要跟着变;全部清空,要归零;清空之后接着用,也必须一切正常。多杀一次、少杀一次,断言都过不去。最后咱们再装一批 `std::string` 进去,确认非平凡类型搬完之后内容完好。

```text
$ ./build/tests/test_vector
VECTOR ALL GREEN
```

到这里,咱们的 Vector 能装、能搬、能遍历,但它还不会拷贝和移动自己:拷贝构造、拷贝赋值都还挂着 `DISABLE_COPY`。下一篇补齐这五件套,顺带做那次 `noexcept` 换来零拷贝的实测。

## 构建与复现

```bash
cd code/volumn_codes/vol8-labs/ministl/stage1_rawbuf_vector
cmake -B build . && cmake --build build
(cd build && ctest --output-on-failure)
./build/example/vector_capacity_trace
```

## 参考资源

- 配套代码:`code/volumn_codes/vol8-labs/ministl/stage1_rawbuf_vector/`
- libstdc++ `bits/stl_vector.h:98-102`(三指针布局)
- [vector 深入:三指针、扩容与迭代器失效](../../vol3-standard-library/containers/03-vector-deep-dive.md)(vol3 概念层,本篇实现层)
- [cppreference:`std::vector` 复杂度](https://en.cppreference.com/w/cpp/container/vector)
