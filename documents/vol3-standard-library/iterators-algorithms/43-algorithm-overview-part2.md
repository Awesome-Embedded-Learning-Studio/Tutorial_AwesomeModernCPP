---
chapter: 7
cpp_standard:
- 11
- 17
- 20
description: 讲透排序、分区与堆这族算法——std::sort 内部为什么是 Introsort（快排+堆排+插入排序，最坏 O(n log n)）、partial_sort 与 nth_element 各自省在哪、make_heap/push_heap/pop_heap 的上浮下沉怎么对应 priority_queue 的底层，以及 C++20 投影怎么让按成员排序不再写自定义比较器
difficulty: intermediate
order: 43
platform: host
prerequisites:
- 迭代器基础与 category
- 迭代器适配器：反向、插入与流，把现成迭代器改出新行为
- 容器适配器：stack、queue、priority_queue 是怎么「包」出来的
reading_time_minutes: 28
related:
- 容器选择指南：按操作、内存与失效规则挑对容器
tags:
- host
- cpp-modern
- intermediate
- 容器
title: 算法总览（下）：排序、分区与堆
---

# 算法总览（下）：排序、分区与堆

上一篇我们把 `<algorithm>` 里那些「不改变元素」和「搬运 / 查找」的算法过了一遍。这一篇换个更重的家族——**会重排整个区间**的那一类：排序、分区、堆。它们有个共同点：都要求**随机访问迭代器**。还记得第 40 篇那个坑吗，`std::sort` 用在 `list` 上连编译都过不去，因为快排要 `it + n` 随机跳转取 pivot。这一整族算法继承同样的限制。

但光知道「要 random_access」还不够。真正的问题是：面对一个具体的排序需求，`sort` / `stable_sort` / `partial_sort` / `nth_element` 这四个长得这么像的家伙，到底该挑哪个？它们内部到底在做什么、复杂度差在哪？还有堆的那一组（`make_heap` / `push_heap` / `pop_heap` / `sort_heap`）——第 09 篇讲 `priority_queue` 时我们提过一句「它的 push 就是 `push_back` + `std::push_heap`」，这一篇就把这套堆操作彻底拆开，看上浮和下沉到底是怎么在数组里挪元素的。最后用 C++20 的投影收个尾：按对象的某个成员排序，再也不用写自定义比较器了。

## 排序家族：四个长得像但各管一段

先把这个家族四兄弟摆在一起。它们处理的都是「把区间按某种顺序重排」，但**承诺的范围不一样**——有的只保证一个元素到位、有的保证前 k 个到位、有的整段都排好。承诺越少，做得越快：

| 算法 | 保证 | 复杂度 |
|------|------|--------|
| `sort` | 整段有序 | O(n log n)，最坏也是 O(n log n) |
| `stable_sort` | 整段有序，相等元素保持原顺序 | O(n log n) 或 O(n log² n)（看内存） |
| `partial_sort` | 前 k 个有序（且是最小的 k 个），后面无序 | 约 O(n log k) |
| `nth_element` | 第 n 个元素恰好落在它排序后该在的位置，左边都比它小、右边都比它大，两边内部无序 | 平均 O(n) |

这张表是这一节的全部精华。看到「我只要前 k 名」「我只要中位数」这种需求时，**别用 `sort` 把整段排完再取**——那是白白多花了 `log n` 倍的功夫。下面一个个拆。

### sort：Introsort，快排为什么不会退化成 O(n²)

`std::sort` 内部不是纯快排。纯快排最让人头疼的是它在已经近乎有序、或者选 pivot 选歪了的输入上会退化成 O(n²)——这也是面试里常考的「快排最坏情况」。标准库显然不能让 `sort` 在某类输入上突然慢一个数量级，所以 libstdc++ 和其它主流实现都用同一个套路：**Introsort**（introspective sort，内省排序），把三种算法的优点拼到一起。

Introsort 的逻辑是这样的：一开始走**快速排序**，快排平均最快；但每递归一层就记一下深度，一旦深度超过 `2·log₂ n` 这个阈值——说明快排可能正在往不平衡的方向退化——就**切到堆排序**（heap sort），堆排序最坏就是 O(n log n)，稳得住；递归到底、子区间很小（通常十几个元素）时，再切到**插入排序**，因为小数据量下插入排序常数小、cache 友好，比继续递归还快。

这三段拼起来，平均性能逼近最快的快排，最坏情况被堆排序兜底锁死在 O(n log n)，小数据量又用插入排序收尾省常数。所以标准对 `std::sort` 的复杂度保证是 **O(n log n)**——具体说就是「应用大约 `N·log(N)` 次比较」，**没有退化余地**。我们上一节表里写的「最坏也是 O(n log n)」就来自这里——Introsort 的「内省」就是它会自己察觉到快排要退化、主动换算法。顺带一提，C++11 之后标准才把这个最坏保证写死（早期 `sort` 的复杂度是「平均 O(n log n)」，最坏没兜底），所以现代实现上你不用再担心快排退化那回事。

::: warning sort 不保证相等元素的顺序
注意 `sort` 不保证相等元素的相对顺序——两个值相同的元素排完谁先谁后是**未指定**的。如果你的逻辑依赖「值相同时保持原来的前后关系」（比如先按入职时间排过、再按薪资排，薪资相同的人不能打乱入职先后），就得用 `stable_sort`。
:::

我们跑一下，直观感受 `sort` 和 `stable_sort` 在「相等元素」上的差别。为了把这个差别看出来，得构造一段**相同 key 很多**的输入——key 全相同的元素越多，非稳定排序把它们打乱的空间越大：

```cpp
// Standard: C++20
#include <algorithm>
#include <iostream>
#include <vector>

struct Point {
    int key;   // 排序依据
    int tag;   // 用来追踪"原始顺序"
};

void print_tagged(const std::vector<Point>& v, const char* lbl)
{
    std::cout << lbl << ": ";
    for (const auto& p : v) std::cout << "{" << p.key << "," << p.tag << "} ";
    std::cout << '\n';
}

int main()
{
    // 3 组 key（1/2/3），每组 8 个 tag 0..7 —— 相等 key 足够多才能看出非稳定
    std::vector<Point> data;
    for (int i = 0; i < 8; ++i) data.push_back({1, i});
    for (int i = 0; i < 8; ++i) data.push_back({2, i});
    for (int i = 0; i < 8; ++i) data.push_back({3, i});

    auto a = data;
    std::sort(a.begin(), a.end(),
              [](const Point& x, const Point& y) { return x.key < y.key; });
    print_tagged(a, "sort        (key 相同的 tag 顺序被算法打乱)");

    auto b = data;
    std::stable_sort(b.begin(), b.end(),
                     [](const Point& x, const Point& y) { return x.key < y.key; });
    print_tagged(b, "stable_sort (key 相同的 tag 仍是 0..7 原顺序)");
    return 0;
}
```

用 `g++ -std=c++20 -O2`（本机 GCC 16.1.1）跑出来：

```text
sort        (key 相同的 tag 顺序被算法打乱): {1,1} {1,2} {1,3} {1,4} {1,5} {1,6} {1,7} {1,0} {2,4} {2,7} {2,6} {2,5} {2,3} {2,2} {2,1} {2,0} {3,0} {3,1} {3,2} {3,3} {3,4} {3,5} {3,6} {3,7}
stable_sort (key 相同的 tag 仍是 0..7 原顺序): {1,0} {1,1} {1,2} {1,3} {1,4} {1,5} {1,6} {1,7} {2,0} {2,1} {2,2} {2,3} {2,4} {2,5} {2,6} {2,7} {3,0} {3,1} {3,2} {3,3} {3,4} {3,5} {3,6} {3,7}
```

`key` 都按 1、2、3 归位了，两个算法这点一样。差别全在「同一组 key 里 tag 的顺序」：`sort` 把 `key=1` 的 tag 重排成了 `1,2,3,4,5,6,7,0`、`key=2` 的重排成了 `4,7,6,5,3,2,1,0`——完全没有保留输入里的 `0..7`；而 `stable_sort` 严格保持 `0,1,2,3,4,5,6,7`。这就是「不稳定」的含义：不是一定会打乱，而是标准**不保证**保留，具体排成什么样由算法内部的交换路径决定，换一份输入、换一个实现都可能不一样。依赖相等元素顺序的代码必须用 `stable_sort`，代价是稳定排序通常需要一块等大缓冲区（开不出来时退化成 O(n log² n)），所以**没那个稳定需求就用 `sort`**，更快也更省内存。

::: warning sort 的相等元素顺序是实现定义的
正因为 `sort` 不保证相等元素顺序，上面这段「打乱成什么样」的输出是 libstdc++ 16.1.1 在这份特定输入上的结果——换了 libc++、MSVC 或换个输入，tag 排列可能完全不同。本篇用它只是为了「直观看见打乱这件事」：真正可移植的结论只有一句——`sort` 的相等元素顺序不可依赖，需要保序就上 `stable_sort`。
:::

### partial_sort：我只要前 k 名，而且要排好

很多需求是「找前 k 名，而且这前 k 名内部还得有序」——比如排行榜只要显示前 10 名、按名次排。`partial_sort(begin, middle, end)` 干的就是这个：排完之后，`[begin, middle)` 这一段是整个区间里最小的 `k = middle - begin` 个元素、而且**内部有序**；`[middle, end)` 这一段保留剩下的元素、**不保证顺序**。

它的做法是在前 k 个位置上维护一个小根堆：线性扫一遍后面的元素，每个元素只要比当前堆顶（前 k 名里最大的那个）小，就替换掉堆顶并下沉调整。扫完整段后，前 k 名就找齐了，最后再对这个小根堆做一次排序。复杂度大约 **O(n log k)**——`k` 越小越省，当 `k` 接近 `n` 时就退化到和完整排序差不多了。

```cpp
// Standard: C++20
#include <algorithm>
#include <iostream>
#include <vector>

void print(const std::vector<int>& v, const char* lbl)
{
    std::cout << lbl << ": ";
    for (int x : v) std::cout << x << ' ';
    std::cout << '\n';
}

int main()
{
    std::vector<int> v{5, 2, 9, 1, 7, 3, 8, 4, 6, 0};
    std::partial_sort(v.begin(), v.begin() + 4, v.end());
    print(v, "partial_sort (前 4 名有序，后面无序)");
    return 0;
}
```

```text
partial_sort (前 4 名有序，后面无序): 0 1 2 3 9 7 8 5 6 4
```

前 4 位 `0 1 2 3` 正好是十个数里最小的四个、而且排好了；后面六个 `9 7 8 5 6 4` 乱序——但别担心，它们确实都比 `3` 大，这就够了。需求是「前 k 名」时，把后半段也排好纯属浪费。

### nth_element：我只要第 n 名，两边不用排

比 `partial_sort` 还激进的场景：我只关心「第 n 大 / 第 n 小那一个元素」是谁，前后两边乱不乱我根本不在乎。最典型的是求**中位数**——`nth_element` 就是给这种需求量身做的。

它内部用的是**快速选择**（quickselect），跟快排同源，但每次分区完只往包含目标位置的那一边递归，另一边直接丢掉。所以平均复杂度是 **O(n)**——比 `sort` 的 O(n log n) 整整少一个对数因子。代价是结果只保证「第 n 位是这个值、左边都比它小（或等）、右边都比它大（或等）」，左右两段内部都是无序的。

```cpp
// Standard: C++20
#include <algorithm>
#include <iostream>
#include <vector>

void print(const std::vector<int>& v, const char* lbl)
{
    std::cout << lbl << ": ";
    for (int x : v) std::cout << x << ' ';
    std::cout << '\n';
}

int main()
{
    std::vector<int> v{5, 2, 9, 1, 7, 3, 8, 4, 6, 0};
    std::nth_element(v.begin(), v.begin() + 4, v.end());
    print(v, "nth_element (第 4 位 = 排序后该在的值，两边无序)");
    std::cout << "  v[4] = " << v[4] << "（10 个数升序排，第 4 位就是 4）\n";
    return 0;
}
```

```text
nth_element (第 4 位 = 排序后该在的值，两边无序): 2 0 1 3 4 5 6 7 8 9
  v[4] = 4（10 个数升序排，第 4 位就是 4）
```

`v[4]` 正好是 `4`——升序排完它就该在这个位置。左边 `2 0 1 3` 全都 `<= 4`，右边 `5 6 7 8 9` 全都 `>= 4`，两边内部都没有顺序。求中位数、求第 k 百分位、求前 k 名但不要求前 k 名内部有序——这些都是 `nth_element` 的主场。

::: warning nth_element 左右两段的内部顺序是实现定义的
和 `sort` 的相等元素顺序一样，`nth_element` 左右两段「内部怎么排」是**未指定**的——标准只保证「第 n 位到位、左边都不大、右边都不小」。上面这段左右两段的具体排列（`2 0 1 3` / `5 6 7 8 9`）是 libstdc++ 16.1.1 在这份输入上的结果，在 libc++ / MSVC 上可能完全不同。真正可移植的结论只有一句：**只信 `v[n]` 这个值到位、左右两段的大小关系**，左右内部的排列不要依赖。
:::

### 四兄弟怎么选

回头看这张家族表，挑选逻辑其实一句话：**你到底要保证多少元素到位**？

- 只要一个元素到位（中位数、第 k 名）→ `nth_element`，平均 O(n)。
- 要前 k 名、且这 k 名内部有序（排行榜）→ `partial_sort`，约 O(n log k)。
- 整段都要有序、不关心相等元素的相对顺序 → `sort`，O(n log n)。
- 整段有序、且相等元素必须保持原顺序 → `stable_sort`。

需求越弱、能用的算法越快。很多人在「我只要求前 10 名」的场景下习惯性 `sort` 再取前 10，数据量一大就慢得明显——这是这族算法最常见的误用。

## 分区：把满足条件的元素挪到一端

分区的目标更轻：不要求有序，只把**满足某个条件的元素全部挪到区间一端**，另一端放不满足的。最常见就是「把偶数挪到前面、奇数挪到后面」。

`std::partition(begin, end, pred)` 原地做这件事，返回一个迭代器指向「分界点」——分界点之前全是满足 `pred` 的、之后全是不满足的。复杂度 O(n)，但它**不稳定**：满足条件的元素之间、不满足的元素之间，相对顺序都可能被打乱。要保序，用 `stable_partition`（代价是有额外内存开销时就 O(n)、没内存就 O(n log n)）。

```cpp
// Standard: C++20
#include <algorithm>
#include <iostream>
#include <vector>

void print(const std::vector<int>& v, const char* lbl)
{
    std::cout << lbl << ": ";
    for (int x : v) std::cout << x << ' ';
    std::cout << '\n';
}

int main()
{
    std::vector<int> v{1, 2, 3, 4, 5, 6, 7, 8, 9};
    auto it = std::partition(v.begin(), v.end(),
                             [](int x) { return x % 2 == 0; });
    print(v, "partition (偶数在前)");
    std::cout << "  分界点在第 " << (it - v.begin()) << " 位\n";

    std::vector<int> w{1, 2, 3, 4, 5, 6, 7, 8, 9};
    std::stable_partition(w.begin(), w.end(),
                          [](int x) { return x % 2 == 0; });
    print(w, "stable_partition (偶数仍是 2,4,6,8 的原顺序)");
    return 0;
}
```

```text
partition (偶数在前): 8 2 6 4 5 3 7 1 9
  分界点在第 4 位
stable_partition (偶数仍是 2,4,6,8 的原顺序): 2 4 6 8 1 3 5 7 9
```

`partition` 把偶数 `8 2 6 4` 全挪到了前面，但顺序和输入里 `2 4 6 8` 完全不一样了——它们是被算法从两头往中间交换时碰巧落成这样的，没有保序保证。而 `stable_partition` 严格保留了 `2 4 6 8` 和 `1 3 5 7 9` 各自的原顺序，付出的代价是它可能需要额外分配一块缓冲区。

### partition_point：在已经分好区的区间上做二分

`std::partition_point(begin, end, pred)` 看起来跟 `partition` 是一对，但它**不会**帮你分区——它的前置条件是**区间已经分好区了**（前面满足 `pred`、后面不满足），它只是在这样一个区间上用二分找到那个分界点，复杂度 O(log n)。

这东西最有用的场景是配合 `partition` / 排过序的区间：分区或排序都是 O(n) 以上的操作，做完一次之后，如果你之后还想反复问「分界点在哪」，就不能每次再扫一遍——直接 `partition_point` 二分一下，O(log n)。

```cpp
// Standard: C++20
#include <algorithm>
#include <iostream>
#include <vector>

int main()
{
    // 注意：这个区间必须已经分区好（偶数全在前、奇数全在后）
    std::vector<int> v{2, 4, 6, 8, 1, 3, 5, 7, 9};
    auto pp = std::partition_point(v.begin(), v.end(),
                                   [](int x) { return x % 2 == 0; });
    std::cout << "分界点在第 " << (pp - v.begin()) << " 位，值是 " << *pp << '\n';
    return 0;
}
```

```text
分界点在第 4 位，值是 1
```

::: warning partition_point 不会替你分区
`partition_point` 假设区间**已经分区好**，它只是二分找分界点。如果区间根本没分区（比如是个乱序区间）就调它，结果是未定义的——它不会帮你重新排。用前先确认区间满足前置条件，通常就是紧跟在 `partition` 或某个保证有序 / 分区的操作之后。
:::

## 堆算法：priority_queue 底层那套，拆开看

第 09 篇我们讲 `priority_queue` 时埋了一句：「它的 `push` 等价于 `c.push_back(x)` + `std::push_heap`，`pop` 等价于 `std::pop_heap` + `c.pop_back()`」。这一节就把这几个 `<algorithm>` 里的堆函数彻底拆开，看它们到底在数组里怎么挪元素。这一节看懂了，`priority_queue` 的行为也就彻底透了。

先回顾堆是个什么东西。**二叉堆**是一棵完全二叉树，存放在数组里：节点 `i` 的左孩子是 `2i+1`、右孩子是 `2i+2`、父节点是 `(i-1)/2`。这个「数组下标 ↔ 树节点」的映射是堆能用数组实现、而且堆操作能 O(log n) 的全部秘密——找父找子都是下标运算，不用指针。最大堆（默认）要求**每个节点都 `>=` 它的孩子**，于是堆顶 `v[0]` 永远是最大值。

标准库给了四个堆操作，对应堆的建立、插入、取出、整体排序：

```cpp
// Standard: C++20
#include <algorithm>
#include <iostream>
#include <vector>

void print(const std::vector<int>& v, const char* lbl)
{
    std::cout << lbl << ": ";
    for (int x : v) std::cout << x << ' ';
    std::cout << '\n';
}

int main()
{
    std::vector<int> h{3, 1, 4, 1, 5, 9, 2, 6};

    // 1) make_heap：把任意区间原地重排成最大堆
    std::make_heap(h.begin(), h.end());
    print(h, "make_heap（堆顶 v[0] 是最大值 9）");

    // 2) push_heap：前提是已经把新元素 push_back 到末尾
    h.push_back(7);
    std::push_heap(h.begin(), h.end());
    print(h, "push_heap(7)（7 从末尾上浮到该在的位置）");

    // 3) pop_heap：把堆顶挪到末尾，剩下的重新下沉成堆
    std::pop_heap(h.begin(), h.end());
    std::cout << "  pop_heap 后，末尾存着刚取出的堆顶 = " << h.back() << '\n';
    h.pop_back();   // 真正把那个最大值从容器里删掉
    print(h, "pop_back 之后");

    // 4) sort_heap：把堆整体排成升序，排完不再是堆
    std::vector<int> s{5, 1, 9, 3, 7, 2, 8, 4, 6, 0};
    std::make_heap(s.begin(), s.end());
    std::sort_heap(s.begin(), s.end());
    print(s, "sort_heap（升序，堆结构被破坏）");
    return 0;
}
```

```text
make_heap（堆顶 v[0] 是最大值 9）: 9 6 4 1 5 3 2 1
push_heap(7)（7 从末尾上浮到该在的位置）: 9 7 4 6 5 3 2 1 1
  pop_heap 后，末尾存着刚取出的堆顶 = 9
pop_back 之后: 7 6 4 1 5 3 2 1
sort_heap（升序，堆结构被破坏）: 0 1 2 3 4 5 6 7 8 9
```

### push_heap 的上浮和 pop_heap 的下沉

这几个函数最反直觉的地方是：**它们都不会帮你改容器大小**。`push_heap` 不插元素，`pop_heap` 也不删元素——它们只在「已经成型」的范围上挪动。这恰恰是 `priority_queue` 把 `push_back` / `pop_back` 和 `push_heap` / `pop_heap` 分两步调的原因。

`push_heap` 的前置条件是：`[begin, end-1)` 已经是个堆，新元素刚刚被 `push_back` 到 `end-1` 位置。它的工作是把这个新元素**上浮（sift-up）**——拿新元素和它的父节点 `(i-1)/2` 比，如果比父大就交换，一路往上走，最多爬树高 `log n` 层。上面的例子里，`make_heap` 完数组是 `9 6 4 1 5 3 2 1`，7 被 `push_back` 到末尾（下标 8），它的父节点是下标 `(8-1)/2 = 3`，也就是值 `1`——7 比 1 大，交换；现在 7 落到下标 3，新父节点是下标 `(3-1)/2 = 1`，也就是值 `6`，6 不比 7 小，停住。7 就这样从末尾「冒」到了下标 3 那一层。关键在于父节点是按下标算的，不是看它在数组里挨着谁——下标 8 的父是下标 3，跟下标 7（值 1）毫无关系。

`pop_heap` 是反过来的**下沉（sift-down）**。它先把堆顶（最大值）和区间末尾元素交换，这样最大值就「腾」到了 `end-1` 位置；然后把换到堆顶的那个新元素一路往下沉——每次跟它两个孩子里较大的那个比，比孩子小就往下换，直到比两个孩子都大为止，同样是 `log n` 层。所以 `pop_heap` 之后，最大值在 `back()` 处（还在容器里），剩下的 `[begin, end-1)` 仍然是个堆——`priority_queue` 接着 `pop_back()` 把那个最大值真正删掉，整个 `pop` 完成。

`sort_heap` 也就是反复 `pop_heap`：每次把当前堆顶挪到区间末尾、再缩短区间范围，循环到空。所以排完是个升序序列——每一步放下去的都是「当前剩下的最大值」，从尾往头正好升序。代价是排完后**堆结构被破坏了**，想再用得重新 `make_heap`。

这几个操作对应的复杂度，正好就是第 09 篇里 `priority_queue` 那张表：

| 堆操作 | 做什么 | 复杂度 |
|--------|--------|--------|
| `make_heap` | 把任意区间原地变成堆 | O(n) |
| `push_heap` | 把末尾新元素上浮到位 | O(log n) |
| `pop_heap` | 把堆顶下沉到末尾 | O(log n) |
| `sort_heap` | 反复 pop，得到升序 | O(n log n) |

所以下一次有人问 `priority_queue` 为什么 `top` 是 O(1)、增删是 O(log n)，你能直接从这几个 `<algorithm>` 函数推出来——`top` 就是读 `c.front()`，常数；`push` 是一次 `push_back`（常数）加一次 `push_heap`（O(log n)）；`pop` 是一次 `pop_heap`（O(log n)）加一次 `pop_back`（常数）。没有任何黑魔法，全是这族堆算法。

::: warning push_heap / pop_heap 不改容器大小
这是新手最容易踩的坑。`push_heap` 不替你 `push_back`——你得先把元素塞到容器末尾再调它；`pop_heap` 也不替你 `pop_back`——它只是把堆顶换到末尾，删不删是你的事。漏了 `push_back`，新元素根本没进去；漏了 `pop_back`，那个"被取出"的最大值还赖在容器末尾。这也是标准库提供 `priority_queue` 的动机之一——它帮你把这两步打包好，免得手写漏掉。
:::

## C++20 投影：按对象的某个成员排序，不写比较器

到这里排序、分区、堆都讲完了，但实战里还有个高频痛点没解决。假设有一堆 `Employee`，想按 `salary` 排序，传统写法得自己写个比较器：

```cpp
std::sort(staff.begin(), staff.end(),
          [](const Employee& a, const Employee& b) { return a.salary < b.salary; });
```

能用，但啰嗦——明明只是「比 salary 字段」，却要写一整个 lambda 把两个对象接进来、再取字段、再比。C++20 的 **ranges** 系列算法（`std::ranges::sort`、`std::ranges::stable_sort`、`std::ranges::nth_element` 等）引入了**投影（projection）**，把这件事压成一个参数。

投影的思路是：你告诉算法「比之前，先对每个元素套这个函数（取出要比较的字段）」，剩下的默认比较规则（`<`）算法自己用。于是上面的排序变成了：

```cpp
std::ranges::sort(staff, {}, &Employee::salary);
```

第二个参数 `{}` 是「用默认比较器」，第三个参数 `&Employee::salary` 就是投影——一个指向成员的指针。算法内部对每对元素比较前，先 `a.salary`、`b.salary` 取出字段再比。不用 lambda、不用提字段名两次，读起来就是「按 salary 排序」这几个字本身。想降序？把 `{}` 换成 `std::greater{}` 即可。

```cpp
// Standard: C++20
#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

struct Employee {
    std::string name;
    int salary;
    int age;
};

std::ostream& operator<<(std::ostream& os, const Employee& e)
{
    return os << "{" << e.name << ", $" << e.salary << ", " << e.age << "}";
}

int main()
{
    std::vector<Employee> staff{
        {"Alice", 9000, 30},
        {"Bob", 12000, 25},
        {"Carol", 9000, 40},
        {"Dave", 7000, 35},
    };

    // 投影 + 默认比较器：按 salary 升序
    auto a = staff;
    std::ranges::sort(a, {}, &Employee::salary);
    std::cout << "ranges::sort 按 &Employee::salary（升序）:\n";
    for (const auto& e : a) std::cout << "  " << e << '\n';

    // 投影 + greater：按 salary 降序
    auto b = staff;
    std::ranges::sort(b, std::greater{}, &Employee::salary);
    std::cout << "ranges::sort 按 &Employee::salary（greater，降序）:\n";
    for (const auto& e : b) std::cout << "  " << e << '\n';

    // stable_sort + 投影：salary 相同时保留输入顺序（Alice 在 Carol 前）
    auto c = staff;
    std::ranges::stable_sort(c, {}, &Employee::salary);
    std::cout << "ranges::stable_sort 按 salary（并列时保输入顺序）:\n";
    for (const auto& e : c) std::cout << "  " << e << '\n';
    return 0;
}
```

```text
ranges::sort 按 &Employee::salary（升序）:
  {Dave, $7000, 35}
  {Alice, $9000, 30}
  {Carol, $9000, 40}
  {Bob, $12000, 25}
ranges::sort 按 &Employee::salary（greater，降序）:
  {Bob, $12000, 25}
  {Alice, $9000, 30}
  {Carol, $9000, 40}
  {Dave, $7000, 35}
ranges::stable_sort 按 salary（并列时保输入顺序）:
  {Dave, $7000, 35}
  {Alice, $9000, 30}
  {Carol, $9000, 40}
  {Bob, $12000, 25}
```

重点看第三组：Alice 和 Carol 薪资都是 9000，`ranges::stable_sort` 严格保留了输入里 Alice 在 Carol 前面的顺序；而第一组的 `ranges::sort` 不保证，两个 9000 的相对顺序是不可靠的。投影是 ranges 系列普遍支持的特性——`sort`、`stable_sort`、`partial_sort`、`nth_element`、`partition` 这些**都有 ranges 版本、都吃投影参数**，逻辑完全一致。

::: warning 指向成员的指针当投影，前提是字段可访问
投影传 `&Employee::salary` 这种指向成员的指针最方便，但要求字段是 `public` 的。`salary` 如果是 `private`，就得么把它暴露出来、要么写一个 getter 函数当作投影传进去（`&Employee::get_salary`），传 lambda 也行（`[](const Employee& e) { return e.salary; }`）。投影不挑可调用物的形态，只要能对单个元素调用、返回要比较的字段就合格。
:::

## C++23 对这一族加了什么

到这里你可能会问：C++23 给排序 / 分区 / 堆加了新东西吗？答案是——**这一族算法本身在 C++23 没有新增**。C++23 给 `<algorithm>` 补的是 `ranges::contains`、`ranges::find_last`、`ranges::starts_with` / `ends_with` 这些**查找类**算法（属于上一篇「非修改式 / 查找」那一组），排序、分区、堆家族的核心 API 在 C++20 的 ranges 化之后就定型了。

我用本机 GCC 16.1.1 在 `-std=c++23` 下编了一遍这几个 ranges 算法，全部正常通过：

```text
g++ -std=c++23 ranges_proj.cpp  →  编译通过，行为与 c++20 一致
```

所以本篇讲的内容（C++20 投影、Introsort、堆算法）在 C++23 / C++26 下原样适用，没有 API 变动需要迁移。要追 C++23 在 `<algorithm>` 的新东西，去看查找那族的 `ranges::contains` / `ranges::find_last`，不是这一篇。

## 小结

排序、分区、堆这一族我们就走完了，几条关键结论收一下：

- **排序四兄弟按「保证多少元素到位」挑**：`nth_element`（只要一个，平均 O(n)）< `partial_sort`（前 k 名且有序，约 O(n log k)）< `sort`（整段有序，O(n log n)）< `stable_sort`（整段有序且相等元素保序）。需求越弱、能用的算法越快。
- **`std::sort` 内部是 Introsort**：快排打底、递归深度超标切堆排兜底（保证最坏 O(n log n)）、小区间切插入排序收尾。所以它没有快排的 O(n²) 退化。
- **分区是更轻的重排**：`partition` 把满足条件的挪到一端（O(n)，不稳）；`stable_partition` 保序（有内存 O(n) / 无内存 O(n log n)）；`partition_point` 只在**已分区**区间上二分找分界点，O(log n)。
- **堆算法是 `priority_queue` 的全部底层**：`make_heap`（建堆，O(n)）/ `push_heap`（上浮，O(log n)）/ `pop_heap`（下沉，O(log n)）/ `sort_heap`（反复 pop 得升序，O(n log n)）。记住 `push_heap` / `pop_heap` **不改容器大小**，`priority_queue` 就是替你把这两步打包好。
- **C++20 投影**：`std::ranges::sort(v, {}, &T::member)` 让按成员排序不再写 lambda，ranges 系列算法普遍支持。
- **C++23 没给这一族加新算法**：排序 / 分区 / 堆的核心 API 在 C++20 就定型了，本机 GCC 16.1.1 的 `-std=c++23` 编这几族算法行为与 C++20 一致。

## 参考资源

- [cppreference: Sorting operations](https://en.cppreference.com/w/cpp/algorithm#Sorting_operations) —— `sort` / `stable_sort` / `partial_sort` / `nth_element` 总览与复杂度
- [cppreference: std::sort](https://en.cppreference.com/w/cpp/algorithm/sort) —— 复杂度保证与 Introsort 的实现约定
- [cppreference: std::nth_element](https://en.cppreference.com/w/cpp/algorithm/nth_element) —— 快速选择与平均 O(n) 的来源
- [cppreference: Partitioning operations](https://en.cppreference.com/w/cpp/algorithm#Partitioning_operations) —— `partition` / `stable_partition` / `partition_point`
- [cppreference: Heap operations](https://en.cppreference.com/w/cpp/algorithm#Heap_operations) —— `make_heap` / `push_heap` / `pop_heap` / `sort_heap`
- [cppreference: std::ranges::sort (C++20)](https://en.cppreference.com/w/cpp/algorithm/ranges/sort) —— 投影（projection）参数说明
