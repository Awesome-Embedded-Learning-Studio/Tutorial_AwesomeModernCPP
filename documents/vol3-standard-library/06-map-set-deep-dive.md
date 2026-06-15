---
title: "map 与 set 深入：红黑树、异构查找与节点句柄"
description: "从红黑树底层实现讲透 std::map 与 set：O(log n) 复杂度与稳定的迭代器、C++14 透明比较器的异构查找、C++17 节点句柄 extract/merge 与改 key 的唯一正道"
chapter: 7
order: 6
tags:
  - host
  - cpp-modern
  - intermediate
  - map
  - 容器
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 17, 20]
reading_time_minutes: 25
prerequisites:
  - "vector 深入：三指针、扩容与迭代器失效"
related:
  - "容器选择指南"
---

# map 与 set 深入：红黑树、异构查找与节点句柄

## 家族合影：map、set 和它们的兄弟

我们用了无数次的 `std::map` 和 `std::set`，日常也就是 `insert`、`find`、遍历，好像没什么神秘的。但只要你往下扒一层，就会发现这俩底下藏着一棵红黑树，而且标准其实从来没有点名让它用红黑树——是三大标准库实现不约而同选了它。更别说 C++14 给它配了异构查找，C++17 又塞进来一个节点句柄，能让你零拷贝搬家，还能顺带改掉那个本该是 const 的 key。这一篇我们就把 map 和 set 从底层到现代用法一次捋清楚。

先认全家族。有序关联容器一共四个亲兄弟，都长在同一棵红黑树上：

| 容器 | 存什么 | 键是否唯一 |
|------|--------|-----------|
| `map` | key → value 键值对 | 唯一 |
| `multimap` | key → value 键值对 | 可重复 |
| `set` | 只存 key | 唯一 |
| `multiset` | 只存 key | 可重复 |

map 和 set 的关系其实很简单：set 就是那个把 value 扔掉、只留 key 的 map，底层节点结构、平衡逻辑、迭代器规矩全都一样。所以这一篇我们以 map 为主线往下讲，set 该有的它都有，区别只在"set 不存 value"这一句话。

至于和邻居的边界，一句话就够：你要的是「有序 + 对数查找」就用 `map`/`set`（红黑树）；要「无序 + 均摊常数查找」就用 `unordered_map`/`unordered_set`（哈希表）；要「有序 + 连续存储（cache 友好）」就上 C++23 的 `flat_map`。三条路线各管一档，这一篇只管红黑树这一条。

## 底下藏着一棵红黑树：标准没点名，但三家都选了它

标准对 map 的要求其实很克制：元素按 key 排好序，查找、插入、删除都是对数复杂度 O(log n)。至于你用什么数据结构达成这个目标，标准说得很模糊——大致是「平衡二叉搜索树」，但没指定具体哪一种。有意思的地方就在这：libstdc++（GCC）、libc++（Clang）、MSVC STL 三家，最后全都选了红黑树。

为什么是红黑树，而不是更「严格平衡」的 AVL 树？关键在删除。AVL 树要求左右子树高度差不超过 1，平衡很紧，代价是删除时可能要从底到顶一路旋转，次数难以控制。红黑树松一些，它只保证「最长路径不超过最短路径的两倍」，换来的是插入最多旋转 2 次、删除最多旋转 3 次——旋转次数有明确上限，对频繁增删的 map 来说更划算。

红黑树的规矩就那么几条，我们快速过一遍（不用背，理解它凭什么保证 O(log n) 就行）：

- 每个节点非红即黑
- 根节点是黑的
- nil 叶子（空哨兵）是黑的
- 红节点的孩子必须是黑（不能两个红连在一起）
- 从任一节点到它所有叶子节点的路径，经过的黑节点数量相同（这个叫「黑高」）

最后两条加在一起，效果就是：你没法让一条路径又长又全是红，因为红不能连排，而黑高又必须一致。于是最长的红黑相间路径，顶多是最短纯黑路径的两倍——树高被压在 O(log n)，查找自然也是 O(log n)。

节点长什么样？和普通二叉搜索树比，就多了一个颜色位和三个指针：

```cpp
// 红黑树节点的简化骨架（标准库内部实现，各厂细节不同，这里只看结构）
struct TreeNode {
    bool      is_red;    // 颜色位
    TreeNode* parent;    // 父节点指针（自底向上调整时要用）
    TreeNode* left;
    TreeNode* right;
    // map 节点这里存 pair<const Key, Value>；set 节点只存 Key
};
```

那个 parent 指针值得多说一句。普通二叉搜索树查找只往下走，不需要知道父亲；但红黑树插入、删除时要自底向上调整颜色、做旋转，得能回头找父亲，所以节点都带了 parent 指针。这也解释了为什么红黑树节点比普通链表节点「重」——它是三叉的。set 在这里和 map 完全同构，唯一差别是节点负载里有没有那个 Value，所以接下来讲 map 的所有机制，你把 Value 抹掉就是 set。

## 复杂度和迭代器失效：和 vector 完全是两套规矩

先把复杂度的账算清楚。红黑树高 O(log n)，所以查找、插入、删除都是顺着树往下走一趟，再加上可能的旋转（旋转本身是 O(1) 的局部操作）。常用操作的复杂度：

| 操作 | 复杂度 |
|------|--------|
| `find` / `count` / `contains` / `operator[]` / `at` | O(log n) |
| `insert` / `emplace` / `erase` | O(log n) |
| 有序遍历 | O(n) |

这里要特别拎出来讲的不是复杂度——红黑树慢点就慢点，很正常——而是**迭代器失效**。map 的失效规矩和 vector 完全是两套，而这恰恰是你在工程里选 map 而不是 vector 的一个硬理由。

vector 我们在[那一篇](03-vector-deep-dive)讲过：一旦扩容，所有迭代器、引用、指针全部失效，因为底层是连续内存、整体搬迁。map 不一样，它的元素是挂在各自独立的树节点上的：

- **插入**：不失效任何已有的迭代器、引用、指针
- **删除**：只失效被删元素本身的那一个迭代器/引用，其他元素纹丝不动

这意味着什么？意味着 map 里元素的地址是稳定的。你可以拿着一个指向 map 元素的指针或引用到处传来传去，只要你不删掉它，这个指针就永远有效。哪怕你在 map 里又插了几千个新元素，或者删了几百个别的元素，手里那个指针照样指着原来那个元素。

这个性质在工程里非常值钱。比方说你写一个事件注册表，每个回调登记进 map 之后，你想把它的指针交给别的子系统去引用、去注销——如果用 vector，一次扩容就把所有指针打成野指针；用 map 就稳稳当当。

我们跑个小例子看一眼这个稳定性：

```cpp
#include <iostream>
#include <map>
#include <string>

int main()
{
    std::map<int, std::string> registry;
    registry[1] = "alpha";
    registry[2] = "beta";

    // 拿一个指向元素 1 的引用和迭代器
    std::string& ref = registry.at(1);
    auto it = registry.find(1);

    // 狂插一堆新元素，触发多次红黑树重平衡
    for (int i = 100; i < 200; ++i) {
        registry[i] = "x";
    }

    // 再删掉一些无关元素
    registry.erase(150);
    registry.erase(160);

    // 原来的引用和迭代器还有效吗？
    std::cout << "ref = " << ref << '\n';
    std::cout << "it = " << it->second << '\n';

    return 0;
}
```

```bash
g++ -std=c++20 -O2 -o /tmp/map_stable /tmp/map_stable.cpp && /tmp/map_stable
```

```text
ref = alpha
it = alpha
```

不管中间插了多少、删了多少（只要没删元素 1 自己），那个引用和迭代器一直有效。这就是红黑树「节点独立挂在堆上」带来的稳定性，也是 map 区别于 vector 的核心工程价值之一。

## 异构查找（C++14）：别再为查找造一个临时 string 了

下面这个坑，写过 string 键 map 的人多半踩过，只是没意识到。看这段：

```cpp
std::map<std::string, int> scores;
scores["alice"] = 90;

auto it = scores.find("alice");   // "alice" 是 const char*
```

`find` 的签名是 `find(const key_type&)`，key_type 是 `std::string`。可你传进去的是个 `const char*`。于是编译器贴心地帮你用 `"alice"` 构造了一个临时的 `std::string`，再拿这个临时对象去查找。一次查找，白搭一个 string 构造——而且 SSO 装不下的话，这个临时 string 还要去堆上分配内存，查完立刻析构释放。你要是在热路径上高频这么查，开销全花在造临时 string 上了。

C++14 给了正解：**透明比较器（transparent comparator）**。

默认情况下 map 的比较器是 `std::less<std::string>`，它只认 string。但标准库还提供了一个特化版本 `std::less<void>`（写成 `std::less<>`），它不绑定具体类型，而是用 `operator<` 直接比较传入的任意两个类型——前提是这俩类型能比。你只要把 map 的比较器声明成 `std::less<>`，它就获得了异构查找能力：

```cpp
#include <map>
#include <string>
#include <string_view>

// 关键：比较器用 std::less<>（透明），而不是默认的 std::less<std::string>
std::map<std::string, int, std::less<>> scores;
scores["alice"] = 90;

// 现在这两种查法都不构造临时 string
scores.find("alice");                    // const char* 直接比
scores.find(std::string_view("alice"));  // string_view 直接比
```

背后的机制是 `is_transparent` 这个嵌套类型。`std::less<>` 内部 typedef 了一个 `is_transparent`，map 的查找重载看到比较器有这个标记，就启用异构版本，直接拿你给的原生类型去和树里的 string 比。string 和 `const char*`、`string_view` 之间本来就支持比较，所以一路畅通，一个临时对象都不构造。

注意两个边界。第一，这要求你的 key 类型和查找类型之间能直接比较——string 和 `const char*` 能比，但你自定义的类型 key 如果没提供和 `string_view` 的比较，就享受不到。第二，异构查找主要在 `find`、`count`、`contains` 这些查找类操作上生效。省临时对象是真的，但「省了就更快」却未必——查找类型用 `const char*` 反而可能更慢（它没有长度缓存，红黑树多次比较里要反复 strlen），得用 `string_view` 才真正提速，这点我们待会儿跑给你看。

## extract 和 merge（C++17）：节点句柄，搬家还能顺便改个 key

C++17 给关联容器塞进来一个叫「节点句柄（node handle）」的东西，听名字挺玄，其实解决的是三个很实在的问题。

先看节点句柄是什么。map 从 C++11 起就有个规矩：key 是 const 的，你拿到一个 map 元素，没法直接改它的 key——`m.begin()->first = 100` 这种写法编译都过不了（key 那个 `first` 是 `const`）。原因也好理解：map 靠 key 排序维持红黑树结构，你要是能随便改 key，树的有序性当场就崩了。

节点句柄绕开了这个限制。`extract` 能把一个节点从树里整个「摘」下来，返回一个独立的节点句柄（类型是 `std::map<K, V>::node_type`）。这个句柄持有节点的所有权，既不在任何 map 里（摘走不影响别的元素），也不拷贝 value——它就是原来那个节点本体。摘下来之后，你可以改它的 key（因为这时候它已经脱离了树，改 key 不会破坏任何有序性），然后再 `insert` 回去。

所以「改 map 元素的 key」这件事，从 C++17 起有了唯一合法的正道：**extract → 改 key → insert**。

```cpp
#include <iostream>
#include <map>
#include <string>

int main()
{
    std::map<int, std::string> m;
    m[1] = "alpha";

    // 直接改 key 编译不过（map 的 key 是 const）
    // m.begin()->first = 100;

    // 正确做法：extract 摘节点，改 key，再 insert
    auto node = m.extract(1);      // 摘下 key=1 的节点
    node.key() = 100;              // 现在能改 key 了（节点已脱离树）
    m.insert(std::move(node));     // 插回去，新 key=100

    std::cout << "count(1)   = " << m.count(1) << '\n';
    std::cout << "count(100) = " << m.count(100) << '\n';
    std::cout << "value      = " << m.at(100) << '\n';

    return 0;
}
```

```bash
g++ -std=c++17 -O2 -o /tmp/map_extract /tmp/map_extract.cpp && /tmp/map_extract
```

```text
count(1)   = 0
count(100) = 1
value      = alpha
```

注意看 value 还是 "alpha"——整个过程中 value 一次都没被拷贝或移动，搬的就是原来那个节点。这就是「零拷贝搬家」。

第二个用途是跨容器迁移节点。两个 map，你想把一个里的某些节点挪到另一个，`extract` + `insert` 就行，同样不拷贝 value：

```cpp
std::map<int, std::string> a, b;
a[1] = "x";
a[2] = "y";

// 把 a 里的节点 1 整个搬到 b
auto node = a.extract(1);
b.insert(std::move(node));
```

第三个用途是 `merge`，一把梭。`m1.merge(m2)` 会把 m2 里所有 key 在 m1 不冲突的节点，整个搬进 m1，同样零拷贝：

```cpp
std::map<int, std::string> m1{{1, "a"}, {2, "b"}};
std::map<int, std::string> m2{{2, "dup"}, {3, "c"}};

m1.merge(m2);
// m1: {1, 2, 3}；m2 里只剩下 key=2 那个（因为 m1 已有 2，冲突没搬走）
```

`merge` 的复杂度是 O(n·log n)（n 是被搬的数量），但全程没有 value 的拷贝——这在迁移大对象（比如 value 是个大 vector 或长字符串）时，省下的开销非常实在。

## 透明比较器到底快不快？跑跑看

先说个题外的事实：libstdc++、libc++、MSVC STL 三家的 map 底层都是红黑树，行为完全一致（这是标准强制的），只是节点布局、内存分配的细节各有各的做法。日常工程不用纠结，知道「行为一致、实现各异」就够了。

但有个更值得亲自验证的问题：透明比较器号称省了临时对象，那它到底快不快？很多人（包括写这篇之前的我）会想当然觉得「省了构造肯定更快」。咱们别猜，直接跑跑看。

准备一个 string 键的 map，key 用长字符串（44 字符，超过 SSO、临时构造要走堆），然后对比三种查法：A 是默认比较器用 `const char*` 查（会构造临时 string）；B 是透明比较器用 `const char*` 查；C 是透明比较器用 `string_view` 查。

```cpp
#include <iostream>
#include <map>
#include <string>
#include <string_view>
#include <chrono>

int main()
{
    std::map<std::string, int> classic;
    std::map<std::string, int, std::less<>> transparent;
    for (int i = 0; i < 10000; ++i) {
        std::string k(40, 'a');
        k += std::to_string(i);
        classic[k] = i;
        transparent[k] = i;
    }
    std::string needle_str(40, 'a');
    needle_str += "9999";
    const char* needle = needle_str.c_str();
    std::string_view needle_sv(needle);
    volatile int sink = 0;

    auto bench = [&](auto fn) {
        auto t0 = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < 100000; ++i) {
            sink += fn()->second;
        }
        auto t1 = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double, std::milli>(t1 - t0).count();
    };

    std::cout << "A classic find(const char*):     "
              << bench([&] { return classic.find(needle); }) << " ms\n";
    std::cout << "B transparent find(const char*): "
              << bench([&] { return transparent.find(needle); }) << " ms\n";
    std::cout << "C transparent find(string_view): "
              << bench([&] { return transparent.find(needle_sv); }) << " ms\n";
    return 0;
}
```

```bash
g++ -std=c++20 -O2 -o /tmp/map_bench3 /tmp/map_bench3.cpp && /tmp/map_bench3
```

```text
A classic find(const char*):     10.5 ms
B transparent find(const char*): 15.5 ms
C transparent find(string_view): 8.7 ms
```

（GCC 16.1.1，本机；具体毫秒数随你的机器变化，但三者的大小关系稳定。）

结果大概率跟你的直觉相反——**B 反而最慢**，C 最快。为什么？关键在 `const char*` 没缓存长度。红黑树一次查找要比较 log(n) 次（这里约 14 次），B 每次拿裸 `const char*` 跟树里的 string 比，都要从头扫到 `'\0'` 算长度（`strlen`），14 次比较就是 14 次 strlen；而 A 虽然先花一次构造临时 string（走堆），但之后那 14 次比较都是 string 对 string，直接用各自缓存的长度做 `memcmp`，反而更快。C 用 `string_view`，构造时算一次长度并缓存下来，后面比较都复用这个长度，既不用每次 strlen、又不构造临时 string，所以最快。

所以记住这个容易踩的坑：**透明比较器要配 `string_view` 才真正提速，配 `const char*` 反而可能更慢**。光是把 `std::less<>` 摆上去、查找类型却用错，性能不升反降。

## 临了收几句

map 和 set 这一家子，表面上看就是「能按键排序、能 O(log n) 查」的容器，底下却是一棵三大实现不约而同选中的红黑树。把它的几个关键性质记牢，以后用 map 心里就有底了：元素地址稳定（插入不失效、删除只失效被删的那个），所以适合做需要稳定句柄的注册表、观察者一类的结构；C++14 的透明比较器让你查 string 键 map 时不再白造临时对象（但记得配 `string_view` 查找才真正提速，用 `const char*` 反而更慢）；C++17 的节点句柄给了你零拷贝搬家和改 key 的唯一合法通道。set 呢，就是把同一套机制里 value 抹掉的那个版本，所有规矩照搬。

下一篇我们顺着这条线，去看 map 的「无序兄弟」`unordered_map`——红黑树的对数查找，换成哈希表的均摊常数查找，是另一种完全不同的取舍。

想直接上手运行看看效果？点开下面的在线示例（能运行、也能看汇编）：

<OnlineCompilerDemo
  title="map / set：红黑树有序、异构查找、extract"
  source-path="code/examples/vol3/06_map_set.cpp"
  description="按键自动有序、std::less<> 透明比较器用 string_view 异构查找、extract 节点零拷贝转移"
  allow-run
/>

## 参考资源

- [std::map — cppreference](https://en.cppreference.com/w/cpp/container/map)
- [std::set — cppreference](https://en.cppreference.com/w/cpp/container/set)
- [std::less\<void\> 透明比较器 — cppreference](https://en.cppreference.com/w/cpp/utility/functional/less_void)
- [map::extract / merge 节点句柄 — cppreference](https://en.cppreference.com/w/cpp/container/map/extract)
- [容器迭代器失效规则总表 — cppreference](https://en.cppreference.com/w/cpp/container#Iterator_invalidation)
- [N3657：C++14 异构查找提案](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2013/n3657.htm)
