---
chapter: 0
cpp_standard:
- 11
- 17
description: flat_map 默认用 vector 存数据,这一篇讲清 vector 的三指针表示、连续存储的 cache 优势、
  扩容与摊还分析、迭代器失效规则,为 flat_map 行为打底
difficulty: intermediate
order: 1
platform: host
prerequisites:
- flat_map 前置知识（零）：有序关联容器与 std::map 的红黑树
reading_time_minutes: 9
related:
- flat_map 前置知识（二）：复杂度与摊还分析
- flat_map 实战（二）：flat_tree 核心骨架
tags:
- host
- cpp-modern
- intermediate
- 容器
- vector
- 优化
title: "flat_map 前置知识（一）：std::vector 内部表示与扩容"
---
# flat_map 前置知识（一）：std::vector 内部表示与扩容

[pre-00](./pre-00-flat-map-ordered-assoc-container-intro.md) 里咱们把 flat_map 的形状描成了"有序数组 + 二分"。那个"数组"到底是谁?翻一眼 Chromium 的模板签名 `flat_map<Key, Mapped, Compare, Container>`,默认那个 `Container` 就是 `std::vector<std::pair<Key, Mapped>>`(flat_map.h:193)。说白了,flat_map 的底座就是 vector。vector 怎么存数据、什么时候扩容、什么时候把迭代器作废,flat_map 就跟着怎么干,跑不掉。

所以这一篇咱们把 `std::vector` 的内部表示彻底拆开。您要是已经对三指针、扩容、迭代器失效这些烂熟,直接跳到后面"回到 flat_map"那一节;不熟的话建议吃透它——后面 flat_tree 每一条复杂度结论,根都埋在这里。

## 三指针:vector 的内部表示

`std::vector<T>` 在 libstdc++、libc++、MSVC 里长得几乎一样:三个指针,加一段连续内存。三个指针塞在一个头结构里,实现之间细节略有出入,概念是一致的。

```text
        begin        end          end_of_storage
          ↓            ↓                ↓
内存:    [ | | | | | | | | | | | | | | | ]
          ←── size ──→←── 空闲 ──→
          ←────────── capacity ────────→
```

`begin` 指头一个元素;`end` 指最后一个元素的下一格,也就是尾后,`end - begin` 正好等于 `size()`;`end_of_storage` 指已分配内存的末尾,`end_of_storage - begin` 就是 `capacity()`,这块内存不扩容时最多能装多少。

笔者要提醒您分清两个量:`size` 是当前真实装了多少个,`capacity` 是这块内存最多能装多少。中间那段(`end` 到 `end_of_storage`)是已经分配下来、但还没排上用场的空闲地。`push_back` 就是直接在这块空地上原地构造新元素,不用再去要内存。

### 连续存储:cache 友好的根

这段内存是连续的,元素一个挨一个,中间没缝(trivially copyable 类型如此;有 alignment padding,但布局仍是连续)。vector 之所以 cache 友好,根就在这。CPU 从内存捞数据是按 cache line 捞的,一条 64 字节,您访问 `data[0]`,旁边的 `data[1]`、`data[2]` 就被免费顺带进 L1;再访问它们,1 个周期,直接命中。[pre-00](./pre-00-flat-map-ordered-assoc-container-intro.md) 里说 flat_map 的常数因子比 `std::map` 小一个数量级,根就是这一段连续内存。

---

## 扩容:capacity 不够时怎么办

`push_back` 走到 `size < capacity`,一拍大腿直接在 `end` 那格构造新元素,`end` 往后挪一格,`O(1)` 收工。这是快乐路径。

可 `size == capacity` 满了怎么办?那就得扩容。整套动作是这样的:先去申请一块更大的新内存,主流实现按 2 倍走,新 `capacity = 旧 capacity * 2`;接着把旧内存里的元素一个一个搬过去——这里有个细节,搬的时候用 move 还是用 copy,取决于元素的 move 是不是 noexcept,是的话放心 move,不是就退回 copy,保住强异常安全;搬完析构旧元素、释放旧内存,再把三个指针拨到新内存上。

这一整套是 `O(n)` 的,得搬 n 个元素。所以单次 `push_back` 的最坏情况就是 `O(n)`,绕不开。

### 摊还 O(1):为什么 push_back 还是"快的"

单次最坏 `O(n)` 听着吓人,但 `push_back` 在工程上是摊还 `O(1)`(amortized constant time)。直觉很简单:扩容那一下是 `O(n)` 不假,可扩完 capacity 翻一倍,接下来 n 次 `push_back` 全踩在快乐路径上,一次都不扩。把那一次 `O(n)` 摊到这 n 次头上,每次平均 `O(1)`。

这就是经典的几何增长容量分析——2 倍扩容把 push_back 的摊还复杂度钉在 `O(1)`。所以日常拿 vector 做 `push_back` 是很快的,那个 `O(n)` 最坏情况您不用怕。

### 回到 flat_map:插入没有摊还

但 flat_map 的单元素插入,这笔摊还的好处它一口都吃不到。

问题出在插入位置。flat_map 要保有序,`insert(key)` 先拿 `lower_bound` 把位置找出来,然后就在那儿插——而那个位置往往在数组中间。中间插一个,后面所有元素都得往后挪一格,这是实打实的 `O(n)` shift,而且**每次插入都要挪**——不像 push_back 只在扩容那一次付代价,其余都是 `O(1)`。所以 flat_map 的单次 insert 就是 `O(n)`,没有摊还这回事。这条您记住,它是后面"查多写少"判据的出处,flat_map 写多了就慢,源头就在这。

---

## 迭代器、指针、引用失效

vector 的失效规则是 C++ 面试老生常谈,但对 flat_map 它是真的承重,咱们这里精确过一遍。

`push_back` 最有意思:没触发扩容时,元素压根没挪窝,所有迭代器、指针、引用一律有效;一旦触发扩容、分配了新内存,旧的整块释放,所有指向旧地址的迭代器瞬间悬垂——全失效。所以工程上保守起见,`push_back` 之后您就把迭代器当作失效处理,别赌它没扩容。`reserve(n)` 同理,`n` 大过当前 capacity 就触发扩容全失效,否则不变。`insert` 和 `erase` 是从操作点开始一直到 `end` 都失效(元素被挪动了),插入还要再叠加一条可能扩容、全失效。`clear` 把所有迭代器都作废,但 capacity 通常留着,要回收得调 `shrink_to_fit`。

### flat_map 故意把规则说粗

Chromium 的 flat_tree 源码里,失效规则的表述是故意保守的——它对所有 mutation(insert / erase / reserve / shrink_to_fit / swap / move ctor / move assign)都甩同一句"Assume that every operation invalidates iterators and references",假设每个操作都让所有迭代器和引用失效。flat_tree.h 那批注释就是这副口气(151/217/231/273/306/319/374 行)。

为什么不安安静静按 vector 的精细规则来——reserve 只在真正 realloc 时失效、insert 只失效从插入点起?笔者琢磨过这事,答案挺务实:那套精细规则对调用方太不友好了,您写代码时得时刻惦记"这次 insert 会不会扩容?这次 reserve 够不够大?",心智负担一重就容易记错。flat_tree 干脆一刀切,改了就当全失效。粗规则不如精细规则"准确",但它没人会记错——而这才是工程上真正要的东西。这件事咱们到 03-5 专门讲迭代器失效时还会回来。

源码里甚至直接甩了一个 UB 例子(flat_map.h:57-60):

```cpp
container["new element"] = it.second;   // UB:operator[] 可能触发扩容,it 失效
```

这种"边遍历边改"的写法,在 flat_map 里就是直接未定义行为,比 `std::map`(节点稳定,迭代器跨 mutation 有效)严格一大截。

## reserve 与 shrink_to_fit

vector 给了两个容量管理接口,flat_tree 也原样透传。

`reserve(n)` 是预先分配能装下 n 个元素的内存。您要是事先知道最终大概要装多少,提前 reserve 一把,后面那几次扩容的搬迁开销全省了。flat_map 批量构造的时候这条特别有用(03-5 会专门讲批量构造模式)。`shrink_to_fit()` 反过来,把 capacity 缩到 size,把多余内存还回去——不过它是个非绑定请求,标准允许实现直接无视,但主流实现一般会配合做一次 realloc。

这俩都会失效迭代器,因为都可能触发 realloc。

---

带着这些,下一篇咱们去把复杂度这套工具讲清楚——`O(lg n)` 查找对着 `O(n)` 插入、摊还对着单次,给后面 flat_tree 的复杂度结论铺好底。

## 参考资源

- [cppreference: std::vector](https://en.cppreference.com/w/cpp/container/vector)
- [cppreference: vector 的迭代器失效规则](https://en.cppreference.com/w/cpp/container/vector#Iterator_invalidation)
- [Chromium `base/containers/flat_tree.h` —— 迭代器失效注释](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/flat_tree.h)
- [Bjarne Stroustrup — vector 与 cache 的性能实验](https://www.stroustrup.com/Software-for-infrastructure.pdf)
