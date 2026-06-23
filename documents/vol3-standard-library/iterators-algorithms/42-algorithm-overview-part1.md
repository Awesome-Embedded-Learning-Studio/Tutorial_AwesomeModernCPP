---
chapter: 7
cpp_standard:
- 11
- 17
- 20
description: 把 <algorithm> 拆成非修改式 / 修改式 / erase-remove 习语 / 有序查找四块讲清选择策略——for_each 为什么不动区间、remove 为什么不真删只搬动、C++20 std::erase 怎么一行搞定删值、binary_search 一族凭什么 O(log n) 以及为何必须先排序
difficulty: intermediate
order: 42
platform: host
prerequisites:
- 迭代器基础与 category
- 迭代器适配器：反向、插入与流，把现成迭代器改出新行为
- vector 深入：三指针、扩容与迭代器失效
reading_time_minutes: 14
related:
- 容器选择指南：按操作、内存与失效规则挑对容器
tags:
- host
- cpp-modern
- intermediate
- 容器
title: 算法总览（上）：非修改式、修改式与查找，面对一个问题怎么挑
---

# 算法总览（上）：非修改式、修改式与查找，面对一个问题怎么挑

上一篇我们讲迭代器适配器时，顺手用了一个小套路——`lower_bound` 找位置 + `insert` 插进去，把一个新元素保序地塞进有序 `vector`。那其实是算法在出场了。现在我们正式进 `<algorithm>` 这一卷。

`<algorithm>` 是 STL 的一大块，里头塞了八十多个算法。如果挨个讲 API 签名，这篇就变成一份枯燥的手册了——那是 cppreference 该干的活，不是我们该干的。我们换一个更有用的视角：**面对一个具体需求，到底该挑哪个算法**。把这一大坨算法按「它对你那片区间干了什么」分成几大类，每类记住两三个代表，复杂度心里有数，遇到问题就能对号入座。

这一篇先讲前四大类：只读不改的**非修改式**、会动元素的**修改式**、专门解决「删元素」的 **erase-remove 习语**（顺带看 C++20 怎么把它简化），还有依赖有序区间的**二分查找**一族。排序、划分、归并这些留到下一篇。所有例子都在本机 GCC 16.1.1 上 `-std=c++20 -O2` 跑过，输出是真实的终端日志。

## 非修改式：只读，连一个元素都不改

第一类最好理解——从头到尾扫一遍，只读不改。`for_each` 遍历、`find` 顺藤摸瓜、`count` 数数、`any_of` 那一队做谓词判定，全属于这一类。它们的共同点是：区间在调用前后一模一样，复杂度基本是 O(n)（二分那族除外，那是后面单独讲的）。

我们先跑一组最常用的，一次把 `for_each` / `find` / `find_if` / `count` / `any_of` / `all_of` / `none_of` 都看一遍：

```cpp
// Standard: C++20
#include <algorithm>
#include <iostream>
#include <vector>

int main()
{
    std::vector<int> v{3, 1, 4, 1, 5, 9, 2, 6};

    // for_each: 只读遍历，不改区间
    int sum = 0;
    std::for_each(v.begin(), v.end(), [&](int x) { sum += x; });
    std::cout << "for_each 求和: " << sum << '\n';

    // find: 线性查找，返回第一个等于目标的迭代器
    auto it = std::find(v.begin(), v.end(), 5);
    std::cout << "find 5 -> 偏移 " << (it - v.begin()) << '\n';

    // find_if: 第一个满足谓词的
    auto big = std::find_if(v.begin(), v.end(), [](int x) { return x > 7; });
    std::cout << "find_if(>7) -> " << (big != v.end() ? *big : -1) << '\n';

    // count / count_if
    std::cout << "count(1): " << std::count(v.begin(), v.end(), 1) << '\n';
    std::cout << "count_if(偶数): "
              << std::count_if(v.begin(), v.end(), [](int x) { return x % 2 == 0; }) << '\n';

    // none_of / any_of / all_of：返回 bool
    std::cout << "any_of(>8): " << std::any_of(v.begin(), v.end(), [](int x) { return x > 8; }) << '\n';
    std::cout << "all_of(<10): " << std::all_of(v.begin(), v.end(), [](int x) { return x < 10; }) << '\n';
    std::cout << "none_of(<0): " << std::none_of(v.begin(), v.end(), [](int x) { return x < 0; }) << '\n';

    return 0;
}
```

跑出来是这样：

```text
for_each 求和: 31
find 5 -> 偏移 4
find_if(>7) -> 9
count(1): 2
count_if(偶数): 3
any_of(>8): 1
all_of(<10): 1
none_of(<0): 1
```

这一族里头要特别拎出来的是 `any_of` / `all_of` / `none_of` 这三兄弟。它们都是**短路求值**的——`any_of` 找到第一个满足谓词的元素就立刻返回 `true`，不会傻乎乎扫完整个区间；`all_of` 遇到第一个不满足的也立刻返回 `false`。所以判断「区间里有没有负数」用 `!std::all_of(..., [](x){return x>=0;})` 也行、用 `std::any_of(..., [](x){return x<0;})` 也行，后者读起来更直接，也更符合「这本来就是个有没有的问题」的思路。

还有一个容易忽略但很实用的：`std::search`。它找的不是单个元素，而是一整段子序列。比如在一段文本里找某个词，`find` 找的是「单个字符等于目标」，`search` 找的才是「这段子串和目标序列逐元素相等」：

```cpp
// Standard: C++20
#include <algorithm>
#include <iostream>
#include <string>

int main()
{
    std::string text = "hello world, hello again";
    std::string needle = "hello";
    auto it = std::search(text.begin(), text.end(), needle.begin(), needle.end());
    std::cout << "search(\"hello\") 第一次偏移: " << (it - text.begin()) << '\n';
    // 从上一次匹配点的下一位继续找第二次出现
    auto it2 = std::search(it + 1, text.end(), needle.begin(), needle.end());
    std::cout << "search 第二次偏移:          " << (it2 - text.begin()) << '\n';
    return 0;
}
```

```text
search("hello") 第一次偏移: 0
search 第二次偏移:          13
```

::: warning 别拿 find 当 search 用
`find` 比的是「单个元素等于目标」，`search` 比的是「一整段子区间逐元素相等」。要在 `vector<int>` 里找一个值用 `find`，要找一个连续子序列（比如 `[3, 4, 5]` 在不在里面）就得 `search`。混了的话，`find` 会返回一个「第一个等于子区间首元素」的位置，跟你想要的「整段匹配」完全不是一回事。
:::

## 修改式：要么就地改，要么写到别处去

第二类会动区间。它分两种作风：**就地改**（原地替换、搬动，区间还是同一个）和**写到目标区间**（源不变，结果写到另一个地方，通常配合上一篇讲的插入迭代器）。

我们还是跑一组把套路都过一遍：

```cpp
// Standard: C++20
#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

void print(const std::vector<int>& v, const char* lbl)
{
    std::cout << lbl;
    for (int x : v) std::cout << x << ' ';
    std::cout << '\n';
}

int main()
{
    std::vector<int> src{1, 2, 3, 4, 5};

    // copy: 原样复制到目标区间
    std::vector<int> copied;
    std::copy(src.begin(), src.end(), std::back_inserter(copied));
    print(copied, "copy:           ");

    // copy_if: 带条件的复制
    std::vector<int> evens;
    std::copy_if(src.begin(), src.end(), std::back_inserter(evens),
                 [](int x) { return x % 2 == 0; });
    print(evens, "copy_if(偶数):  ");

    // transform: 一对一映射，把每个元素变身后写到目标
    std::vector<int> squared;
    std::transform(src.begin(), src.end(), std::back_inserter(squared),
                   [](int x) { return x * x; });
    print(squared, "transform(x*x): ");

    // replace / replace_if: 就地把满足条件的元素换成新值
    std::vector<int> r{1, 2, 3, 2, 4, 2};
    std::replace(r.begin(), r.end(), 2, 99);
    print(r, "replace(2->99): ");

    std::vector<int> r2{1, 2, 3, 4, 5, 6};
    std::replace_if(r2.begin(), r2.end(), [](int x) { return x % 2 == 0; }, 0);
    print(r2, "replace_if(偶->0): ");

    // unique: 就地去重相邻重复（关键看后面 erase-remove 段）
    std::vector<int> u{1, 1, 2, 3, 3, 3, 4, 1, 1};
    auto new_end = std::unique(u.begin(), u.end());
    std::cout << "unique 后逻辑终点偏移: " << (new_end - u.begin())
              << " 实际 size 仍为 " << u.size() << '\n';

    // move: 把元素搬走（右值），目标拿到所有权
    std::vector<std::string> words{"aa", "bb", "cc"};
    std::vector<std::string> moved;
    std::move(words.begin(), words.end(), std::back_inserter(moved));
    std::cout << "move 后源区间首元素 size: " << words[0].size() << '\n';

    return 0;
}
```

```text
copy:           1 2 3 4 5
copy_if(偶数):  2 4
transform(x*x): 1 4 9 16 25
replace(2->99): 1 99 3 99 4 99
replace_if(偶->0): 1 0 3 0 5 0
unique 后逻辑终点偏移: 5 实际 size 仍为 9
move 后源区间首元素 size: 0
```

这里头有两组「就地 vs 写到别处」的对照值得记住：

- **改值**：就地用 `replace` / `replace_if`；想要结果落到新区间，就用 `replace_copy` / `replace_copy_if`（这俩名字里带 `_copy` 的，等于「replace + copy 一步到位」，源不动）。
- **搬元素**：就地重排用 `move`（把源区间的元素搬走，留下的是「已移动」的空壳——上面 `words[0].size()` 变 0，就是字符串内容被搬走的证据）；要把变换结果拷贝到新区间，用 `transform`。

`unique` 这条我们等下单独开一节讲，因为它和 `remove` 是一对孪生兄弟，都带着同一个反直觉的设计——**只搬动、不缩容**。这正是 STL 最经典的坑之一，也是接下来这一节的主角。

## erase-remove 习语：为什么 remove 不真删

这是 STL 里最经典、也最容易把新手绊倒的一个设计。需求很简单：把 `vector` 里所有等于 `2` 的元素删掉。第一反应大概是找一个叫 `remove` 的算法——还真有，`std::remove`。但它**不会真的删任何东西**。

先看它到底干了什么：

```cpp
// Standard: C++20
#include <algorithm>
#include <iostream>
#include <vector>

int main()
{
    std::vector<int> v{1, 2, 3, 2, 4, 2, 5};
    std::cout << "原始:                ";
    for (int x : v) std::cout << x << ' ';
    std::cout << "  [size=" << v.size() << "]\n";

    auto new_end = std::remove(v.begin(), v.end(), 2);
    std::cout << "remove(2) 后逻辑终点偏移: " << (new_end - v.begin()) << '\n';
    std::cout << "remove 后物理内容:    ";
    for (int x : v) std::cout << x << ' ';
    std::cout << "  [size 仍为 " << v.size() << "]\n";
    return 0;
}
```

```text
原始:                1 2 3 2 4 2 5   [size=7]
remove(2) 后逻辑终点偏移: 4
remove 后物理内容:    1 3 4 5 4 2 5   [size 仍为 7]
```

看出来了吧——`remove` 干的事，是把「不等于 2 的元素」往前搬，挤到区间前半段，然后返回一个**新的逻辑终点**。但 `vector` 的物理大小一点没变，还是 7 个元素，后半段留下的是搬动后残留的旧值（`4 2 5` 那几个），属于「逻辑上已经被废弃、但物理上还占着格子」的垃圾。

### 为什么不直接删：算法不认识容器

这个设计看起来很别扭，但原因讲通了其实很合理：**`std::remove` 只认迭代器，不认容器**。上一篇我们讲过，算法靠迭代器接口和容器解耦——`remove` 拿到的就是两个迭代器，它根本不知道这俩迭代器背后挂的是 `vector`、`list` 还是 `deque`，更无从知道该调谁的 `erase` 来真正缩容。erase 是容器成员函数，不是算法该管的。所以 `remove` 只能做它力所能及的事：搬元素、返回新终点，缩容这件事交还给调用者。

于是真正的删除得两步走——`remove` 搬完，再拿容器自己的 `erase` 把新终点之后的尾巴切掉：

```cpp
v.erase(new_end, v.end());
```

```text
erase 后:             1 3 4 5   [size=4]
```

这两步合起来就是大名鼎鼎的 **erase-remove 习语**：

```cpp
v.erase(std::remove(v.begin(), v.end(), 2), v.end());
```

`unique` 的套路一模一样——它只把相邻重复「挤掉」，同样只搬动、不缩容，真删也得配 `erase`。前面那段 `unique` 的输出就是证据：逻辑终点偏移 5，但 `size` 还是 9，得再 `u.erase(new_end, u.end())` 才真的少掉那几个元素。所以记一个口诀就够：**`remove` / `unique` 只搬动，缩容永远靠 `erase`**。

### C++20：std::erase / erase_if 让这事一行搞定

上面那串 `erase(remove(...), end())` 写多了是真烦。C++20 给了一组新的自由函数——`std::erase(c, value)` 和 `std::erase_if(c, pred)`，直接吃容器、直接删值或删满足条件的元素，内部自动把 erase-remove 那套替你干了，还顺手返回删掉了几个：

```cpp
// Standard: C++20
#include <algorithm>
#include <iostream>
#include <vector>

void print(const std::vector<int>& v, const char* lbl)
{
    std::cout << lbl;
    for (int x : v) std::cout << x << ' ';
    std::cout << "  [size=" << v.size() << "]\n";
}

int main()
{
    std::vector<int> w{1, 2, 3, 2, 4, 2, 5};
    auto erased = std::erase(w, 2);
    std::cout << "std::erase(w, 2) 删了 " << erased << " 个\n";
    print(w, "结果:                 ");

    std::vector<int> x{1, 2, 3, 4, 5, 6, 7, 8};
    auto erased_if = std::erase_if(x, [](int n) { return n % 2 == 0; });
    std::cout << "std::erase_if(偶数) 删了 " << erased_if << " 个\n";
    print(x, "结果:                 ");

    return 0;
}
```

```text
std::erase(w, 2) 删了 3 个
结果:                 1 3 4 5   [size=4]
std::erase_if(偶数) 删了 4 个
结果:                 1 3 4 5 7   [size=4]
```

是不是清爽多了。既然有了它，旧的 erase-remove 习语是不是可以彻底忘了？**不完全能**。这里有个适用范围的细节，是拿本机 GCC 16.1.1 实测出来的：

- **序列容器**（`vector` / `string` / `deque` / `list` / `forward_list`）：`erase(c, value)` 和 `erase_if(c, pred)` **两个都有**。
- **关联容器**（`map` / `set` / `multimap` / `multiset` 以及它们的 `unordered_` 变种）：**只有 `erase_if`，没有按值的 `erase`**。

为什么关联容器没有按值的 `erase`？因为它们本身就有一个成员 `c.erase(key)`——按 key 删一个节点。要是自由函数 `std::erase(c, value)` 也存在，名字撞车、语义还微妙不同，标准委员会干脆只给关联容器配 `erase_if`。我们在 GCC 16.1.1 上实测，给 `std::set` 调 `std::erase(s, 2)` 直接编不过：

```text
error: no matching function for call to 'erase(std::set<int>&, int)'
  7 |     std::erase(s, 2);   // 关联容器: 只有 erase_if，没有按值的 erase
```

报错很直白：找不到匹配的 `erase`。所以记一句话——**关联容器删元素用 `erase_if`，序列容器删值 `erase`、删条件 `erase_if` 都行**。序列容器上能写一行就别再写 `erase(remove(...), end())` 那串了。

::: warning ranges::remove 返回的是 subrange，不是裸迭代器
C++20 还给了 ranges 版的 `std::ranges::remove`，它返回的不再是一个裸的「新终点迭代器」，而是一个 `subrange`（保留区间 + 废弃区间那对迭代器的组合）。配 `erase` 时要这么写：

```cpp
auto [first, last] = std::ranges::remove(v, 2);
v.erase(first, last);
```

把它和经典版的 `v.erase(std::remove(...), v.end())` 混着记容易绕晕，好在序列容器直接用 `std::erase` / `erase_if` 一行最省事，ranges 版的 remove 平时少写。
:::

## 有序查找：二分那一族，O(log n) 的前提是已排序

到这里为止前面讲的 `find`、`count` 都是 O(n) 的线性扫——数据量一大就现原形。有没有更快的查法？有，前提是**区间已经排好序**。一旦有序，二分查找就能把复杂度从 O(n) 砍到 O(log n)。

这一族有四个，分工不同：

- `binary_search(first, last, v)` —— 只回答「v 在不在」，返回 `bool`。
- `lower_bound(first, last, v)` —— 返回第一个「**不小于** v」（`>= v`）的位置。
- `upper_bound(first, last, v)` —— 返回第一个「**大于** v」（`> v`）的位置。
- `equal_range(first, last, v)` —— 一次返回 `[lower, upper)`，也就是 v 在区间里的完整范围。

光看描述 `lower_bound` 和 `upper_bound` 容易混。我们直接跑一遍，让输出说话：

```cpp
// Standard: C++20
#include <algorithm>
#include <iostream>
#include <vector>

int main()
{
    std::vector<int> v{1, 3, 3, 5, 7, 7, 7, 9};   // 已升序

    // binary_search: 在不在（bool）
    std::cout << "binary_search(7): " << std::binary_search(v.begin(), v.end(), 7) << '\n';
    std::cout << "binary_search(4): " << std::binary_search(v.begin(), v.end(), 4) << '\n';

    // lower_bound: 第一个「不小于」value 的位置（>= value）
    auto lo = std::lower_bound(v.begin(), v.end(), 7);
    std::cout << "lower_bound(7) -> 偏移 " << (lo - v.begin()) << " 值 " << *lo << '\n';

    // upper_bound: 第一个「大于」value 的位置（> value）
    auto up = std::upper_bound(v.begin(), v.end(), 7);
    std::cout << "upper_bound(7) -> 偏移 " << (up - v.begin()) << " 值 " << *up << '\n';

    // equal_range: [lower, upper) 就是 7 的完整范围
    auto [eq_lo, eq_up] = std::equal_range(v.begin(), v.end(), 7);
    std::cout << "equal_range(7): [" << (eq_lo - v.begin()) << ", " << (eq_up - v.begin()) << ") -> ";
    for (auto it = eq_lo; it != eq_up; ++it) std::cout << *it << ' ';
    std::cout << "共 " << (eq_up - eq_lo) << " 个\n";

    // 查一个不存在的值：lower_bound 给的是「该插哪」
    auto lo4 = std::lower_bound(v.begin(), v.end(), 4);
    std::cout << "lower_bound(4) -> 偏移 " << (lo4 - v.begin()) << " 值 " << *lo4
              << "（4 不在，指向插入点）\n";
    return 0;
}
```

```text
binary_search(7): 1
binary_search(4): 0
lower_bound(7) -> 偏移 4 值 7
upper_bound(7) -> 偏移 7 值 9
equal_range(7): [4, 7) -> 7 7 7 共 3 个
lower_bound(4) -> 偏移 3 值 5（4 不在，指向插入点）
```

对着输出看就清楚了：三个 `7` 占了偏移 4、5、6 三格，`lower_bound(7)` 落在第一个 `7`（偏移 4，`>= 7` 的起点），`upper_bound(7)` 落在 `7` 之后的第一个 `9`（偏移 7，`> 7` 的起点），`equal_range` 一次性把 `[4, 7)` 这段半开区间给你。而查一个不在的值 `4`，`lower_bound` 落在偏移 3（指向 `5`）——这正是「要是把 4 插进来该放哪」的位置。

### 承接上篇：insert_sorted 就是 lower_bound + insert

现在回过头看，上篇那个「保序插入」的小套路就完全顺了。`lower_bound` 在有序区间上 O(log n) 找到插入点，再用容器的 `insert` 把元素塞进去，搬运那一下躲不掉（连续存储，O(n)），但找位置这一步被二分压到了对数级：

```cpp
// Standard: C++20
#include <algorithm>
#include <iostream>
#include <vector>

int main()
{
    std::vector<int> sorted{1, 3, 5, 7, 9};
    int new_val = 4;
    auto pos = std::lower_bound(sorted.begin(), sorted.end(), new_val);
    sorted.insert(pos, new_val);
    std::cout << "insert_sorted(4): ";
    for (int x : sorted) std::cout << x << ' ';
    std::cout << '\n';
    return 0;
}
```

```text
insert_sorted(4): 1 3 4 5 7 9
```

### 二分到底比线性快多少：上手跑一跑

光说「O(log n) 比 O(n) 快」有点空。我们直接拿一千万个元素的有序 `vector`，在最坏情况下（目标在末尾）对比 `find` 和 `binary_search`，看真实差距：

```cpp
// Standard: C++20
#include <algorithm>
#include <chrono>
#include <iostream>
#include <vector>

int main()
{
    constexpr int kN = 10'000'000;
    std::vector<int> v(kN);
    for (int i = 0; i < kN; ++i) v[i] = i;   // 已升序

    int target = kN - 1;   // 最坏情况：在末尾

    auto t1 = std::chrono::high_resolution_clock::now();
    bool found_lin = std::find(v.begin(), v.end(), target) != v.end();
    auto t2 = std::chrono::high_resolution_clock::now();
    bool found_bin = std::binary_search(v.begin(), v.end(), target);
    auto t3 = std::chrono::high_resolution_clock::now();

    auto us_lin = std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count();
    auto us_bin = std::chrono::duration_cast<std::chrono::microseconds>(t3 - t2).count();

    std::cout << "find        (O(n))      " << found_lin << "  耗时 " << us_lin << " us\n";
    std::cout << "binary_search (O(log n)) " << found_bin << "  耗时 " << us_bin << " us\n";
    std::cout << "倍数差距: " << (us_bin > 0 ? us_lin / us_bin : -1) << "x\n";
    return 0;
}
```

本机 GCC 16.1.1、`-O2` 跑出来（单次测量，具体微秒数会随机器和运行波动，数量级是稳定的）：

```text
find        (O(n))      1  耗时 5891 us
binary_search (O(log n)) 1  耗时 1 us
倍数差距: 5891x
```

一千万个元素，线性 `find` 在最坏情况下要扫到底，落在毫秒级；二分几次比较就定位到，落在微秒级，量级上差了几千倍。这就是「有序」带来的红利——前提是你确实保持有序。

### 真正的坑：在没排序的区间上用二分

二分族对区间的「已排序」是**硬前提**，不是「排了更好、不排也凑合」。标准里写的是 preconditions，违反它就是**未定义行为**——编译器不会拦你，结果也完全靠不住。我们在一段故意打乱的序列上跑一下，让坑现形：

```cpp
// Standard: C++20
#include <algorithm>
#include <iostream>
#include <vector>

int main()
{
    // 一个会让 binary_search 漏判的未排序序列
    std::vector<int> u{10, 1, 30, 2, 20, 3};   // 含 2，但无序
    std::cout << "实际含 2?        " << (std::find(u.begin(), u.end(), 2) != u.end()) << '\n';
    std::cout << "binary_search(2):" << std::binary_search(u.begin(), u.end(), 2) << '\n';
    return 0;
}
```

```text
实际含 2?        1
binary_search(2):0
```

`2` 明明在区间里（`find` 找到了），`binary_search` 却返回 `0`——因为二分算法假设区间有序，会朝着「2 应该出现在前半段」的方向找，找不到就当没有。这不是 bug，是我们没满足它的前提。所以用二分族之前，先确认区间真的有序；不确定就老老实实用 `find`，O(n) 慢点但至少不会骗你。

::: warning 二分的前提是「已排序」，且比较语义要一致
两个常被忽略的前置条件：一是区间必须已排序，二是排序用的比较器和查找用的比较器语义要一致（你按降序排的，`binary_search` 默认按升序找，照样错）。`binary_search` / `lower_bound` / `upper_bound` / `equal_range` 都接受一个额外的比较器参数，排序比较器和它对不上时，一定要把这个参数传进去。先排序、后查找、比较器一致——这三件事齐了，二分族才靠谱。
:::

## 按需求挑算法：一张决策表

讲了这么多，落到实战就是一个问题——「我这个需求，该用哪个」。我们把这一篇覆盖的场景汇成一张决策表，照着对号入座就行：

| 我要干的事 | 区间状态 | 选谁 | 复杂度 |
|---|---|---|---|
| 判断「有没有满足条件的元素」 | 任意 | `any_of` / `all_of` / `none_of` | O(n)，短路 |
| 数「有多少个满足条件」 | 任意 | `count_if` | O(n) |
| 找第一个满足条件的元素 | 任意 | `find_if` | O(n) |
| 找一段连续子序列 | 任意 | `search` | O(n·m) |
| 把每个元素变身后放到新区间 | 任意 | `transform` | O(n) |
| 就地把满足条件的元素改值 | 任意 | `replace_if` | O(n) |
| 删掉所有等于某值的元素（序列容器） | 任意 | `std::erase(c, value)` | O(n) |
| 删掉所有满足条件的元素（任意容器） | 任意 | `std::erase_if(c, pred)` | O(n) |
| 删掉所有等于某值的元素（C++20 前） | 任意 | `erase(remove(...), end())` 习语 | O(n) |
| 去掉相邻重复 | 先排序更有效 | `unique` + `erase` | O(n) |
| 判断「某值在不在」 | **已排序** | `binary_search` | O(log n) |
| 找第一个「不小于 / 大于」某值的位置 | **已排序** | `lower_bound` / `upper_bound` | O(log n) |
| 找某值的完整出现范围 | **已排序** | `equal_range` | O(log n) |
| 保序插入一个新元素 | **已排序** | `lower_bound` 找点 + `insert` | O(log n) + O(n) |

这张表就是这一篇的收口。记住一条总原则——**查改删 O(n) 是默认档，能排好序才有 O(log n) 的二分红利**。

## 小结

- `<algorithm>` 按对区间干了什么分四类：非修改式（只读）、修改式（就地改或写到目标）、erase-remove 习语（删元素）、有序查找（二分族）。
- `any_of` / `all_of` / `none_of` 短路求值；`search` 找子序列不是单元素。
- `remove` / `unique` **只搬动、不缩容**，返回新逻辑终点，缩容永远靠容器的 `erase`——这是 STL 最经典的坑。
- C++20 的 `std::erase` / `erase_if` 自由函数让删元素一行搞定；序列容器两个都有，关联容器只有 `erase_if`。
- 二分族（`binary_search` / `lower_bound` / `upper_bound` / `equal_range`）把查找压到 O(log n)，前提是**区间已排序**、且比较器语义一致；在没排序的区间上用二分是未定义行为，会给出错的答案。

下一篇我们接着讲算法这一块的下半场——排序（`sort` / `stable_sort` / `partial_sort`）、划分（`partition`）、归并（`merge`），还有「已排序区间」这套前提下更多 O(log n) 的玩法。

## 参考资源

- [cppreference: Algorithms library](https://en.cppreference.com/w/cpp/algorithm) —— `<algorithm>` 全家桶总览，按非修改式 / 修改式 / 划分 / 排序 / 二分等分类
- [cppreference: std::remove](https://en.cppreference.com/w/cpp/algorithm/remove) —— erase-remove 习语里 remove 的「只搬动、不缩容」机制
- [cppreference: std::erase, std::erase_if (C++20)](https://en.cppreference.com/w/cpp/container/erase) —— 统一删除自由函数，各容器特化与适用范围
- [cppreference: std::lower_bound](https://en.cppreference.com/w/cpp/algorithm/lower_bound) —— 二分一族「第一个不小于」的语义与复杂度
- [cppreference: std::binary_search](https://en.cppreference.com/w/cpp/algorithm/binary_search) —— 二分前提（已排序）与未定义行为说明
