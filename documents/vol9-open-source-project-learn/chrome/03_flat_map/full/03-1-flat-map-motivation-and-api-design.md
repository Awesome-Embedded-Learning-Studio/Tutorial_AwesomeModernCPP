---
chapter: 1
cpp_standard:
- 17
- 20
description: 从配置表的 cache miss 痛点切入,讲清 flat_map 要补的洞,定下完整目标 API,
  含 at 的 CHECK、默认透明比较器、sorted_unique 构造等关键决策
difficulty: intermediate
order: 1
platform: host
prerequisites:
- flat_map 前置知识（零）：有序关联容器与 std::map 的红黑树
- flat_map 前置知识（二）：复杂度与摊还分析
reading_time_minutes: 11
related:
- flat_map 实战（二）：flat_tree 核心骨架
tags:
- host
- cpp-modern
- intermediate
- 容器
- map
- 优化
title: "flat_map 实战（一）：动机与接口设计"
---
# flat_map 实战（一）：动机与接口设计

[前置知识（零）](./pre-00-flat-map-ordered-assoc-container-intro.md) 里笔者埋了个问题:std::map 的红黑树每个节点一次 malloc,查找时指针追逐一路踩 cache miss,渐近复杂度是 `O(log n)` 没错,可一到小到中等数据量、查多写少的场景,常数因子就把性能拖垮了。这一篇咱们就回到这个痛点,把 flat_map 到底要补哪些洞想明白,顺手把目标 API 一次定死。

flat_map 想干的事其实一句话能说完:给"写一次读多次"的有序映射一个 cache 友好的实现。它没打算抢 std::map 的饭碗,"大且频繁改"那块地盘 std::map 照样赢;它补的是"查多写少"这个空缺。这个系列咱们一边拆 Chromium 怎么实现,一边自己手搓一个教学版,对照着看更容易想明白里头的取舍。

---

## 从一个性能痛点说起:配置表

假设咱们在写一个命令分发表。程序启动时从配置文件加载一堆命令→回调的映射,跑起来之后就只剩查找,不再改:

```cpp
std::map<std::string, Handler> commands;
for (auto& [name, handler] : load_commands()) {
    commands.emplace(name, handler);   // 启动期构造一次
}

// 运行期:每条命令只查找
auto it = commands.find(cmd_name);
if (it != commands.end()) it->second(args);
```

这段代码看着挑不出毛病,`std::map` 的 `O(log n)` 查找听起来也"够快"。可您拿 perf 一跑就傻眼了:大量时间堆在 `std::map::find` 上,而表里可能就几十个元素。渐近复杂度 `O(log n)` 没告诉您的是,每次 `find` 的那 `log n` 步,每步都是一次大概率 cache miss 的指针解引用——几十个元素,`log n` 也就六七步,每步都 miss 一下,这查找就废了。

[pre-00](./pre-00-flat-map-ordered-assoc-container-intro.md) 里咱们拆过根因:红黑树节点散落堆上,每节点 32 字节元数据外加一次堆分配,查找时 `node = node->left_` 是数据相关的解引用,CPU 想预取都摸不准下个地址在哪。数据量一小,这"常数因子"反而比 `log n` 主导得多。这就是 std::map 在这个场景下扎您的地方。

---

## 三种现成解法为什么都不够

痛点清楚了,那现成的轮子能凑合用吗?咱们挨个看。

`std::map` 就不用再说了,上面的 cache miss 就是它干的,每节点一次 malloc 的毛病也治不好。

那换 `std::unordered_map` 呢?哈希表平均 `O(1)` 查找,听着美。可小数据量下它的常数因子未必讨得到便宜——哈希计算加上处理碰撞的开销摆在那。更要命的是它无序,您没法按 key 顺序遍历,没法 `lower_bound` 找范围,也没法做有序区间查询。命令分发表哪天产品要"列出所有命令"或者"按前缀过滤一下",unordered_map 当场抓瞎。Chromium 自己的容器指南也明说了不推荐 `std::unordered_map`,性能干不过 absl 那套 hash map。

剩下一条路是手搓:自己维护一个 `vector<pair<K,V>>`,每次插入后 `sort`,查找用 `std::lower_bound`。功能上跟 flat_map 没差,可您得自己处理去重、自己保持有序、自己惦记迭代器失效、想要 sorted_unique 优化还得自己加标签 dispatch……纯重复造轮子,而且每一步都容易写错。笔者自己搓过一版,后来发现就是在 flat_map 该有的坑里一个个重踩。

三条路都顶不住:map cache 痛,unordered_map 无序又不受推荐,手搓是重复造还容易错。Chromium 的回答干脆——把这些功能打包成一个有 std::map 风格接口的容器,叫 `flat_map`。

---

## Chromium 的回答:flat_map 设计哲学

flat_map 的设计哲学说白了就两件事。第一件,存储用一段连续的有序数组(默认就是 `vector<pair<K,V>>`),查找走二分(`lower_bound`)——连续换来了 cache 友好,二分换来了 `O(log n)` 查找。第二件,它压根不追求插入性能,单次插入 `O(n)` 的 shift 它认了,换回来的好处是构造期一把排序(批量构造 `O(N log N)`)、查找期那个 cache 友好的低常数因子。

这两条就把 flat_map 的适用边界画出来了:写一次读多次,或者数据量始终很小的有序映射。要是您的场景是"大且频繁改",那 `O(n)` 的插入会疼得您怀疑人生——那是 std::map 的主场,别来凑热闹。

### 架构概览:flat_tree 是唯一实现

flat_map 的实现里有个挺优雅的分层,笔者第一次读到的时候愣了一下:核心其实只有一个类,叫 `flat_tree<Key, GetKeyFromValue, KeyCompare, Container>`,它是个通用的"有序数组关联容器"。`flat_map` 和 `flat_set` 都是它的薄壳。

具体怎么薄法?`flat_map<Key, Mapped, ...>` 继承自 `flat_tree<Key, internal::GetFirst, ...>`,这里的 `GetFirst` 是个提取器,从 `pair<Key, Mapped>` 里抠出 `first` 当 key(flat_map.h:194-195、24-29)。`flat_set<Key, ...>` 更干脆,直接是 `flat_tree<Key, std::identity, ...>` 的别名,一个 `using =`,`std::identity` 把 value 原样当 key(flat_set.h:159-163)。

您品一下这个设计——一份 flat_tree 实现,光靠换"key 提取器"这一行,就同时拿出了 map 和 set 两副面孔。这就是策略对象的经典玩法,理解了 flat_tree,flat_map 和 flat_set 也就白送了。所以咱们这个系列的实战篇主要就在拆 flat_tree,flat_map 跟 flat_set 的差异,全在那行提取器上。

---

## 设计目标 API

动机讲完了,咱们把目标 API 一次定死,再回头抠每个签名里的决策。命名沿用 `tamcpp::chrome` 命名空间,snake_case 风格,跟 OnceCallback、WeakPtr 那两个系列保持一致。

### 构造

```cpp
#include "flat_map/flat_map.hpp"
using namespace tamcpp::chrome;

// 从无序数据构造(内部排序去重)
flat_map<int, std::string> m1 = {{1, "a"}, {3, "c"}, {2, "b"}};

// 从已有 vector move 构造(批量构造,高效)
std::vector<std::pair<int, std::string>> raw = {{1,"a"}, {2,"b"}, {3,"c"}};
flat_map<int, std::string> m2(std::move(raw));

// sorted_unique 构造(数据已有序,跳过排序)
flat_map<int, std::string> m3(sorted_unique, std::vector<std::pair<int,std::string>>{{1,"a"},{2,"b"},{3,"c"}});
```

### 查找与修改

```cpp
flat_map<int, Config> m;
m[1] = load(1);              // operator[]:缺失则插入
m.insert_or_assign(2, x);    // 插入或覆写
m.try_emplace(3, arg1, arg2);// 仅 key 不存在时构造 mapped

auto it = m.find(1);         // O(log n) 二分
if (it != m.end()) use(it->second);

m.at(99);                    // 越界 → CHECK 崩溃(非 throw,见决策分析)
```

### 异构查找(透明比较器)

```cpp
flat_map<std::string, Config> sm;        // 默认 Compare = std::less<>(透明)
sm.find("timeout");                       // 直接用 const char* 查,不构造临时 std::string
```

---

## 接口设计决策分析

API 定是定下来了,可每个签名里都藏着取舍,咱们把"为什么"挨个抠清楚。

### 为什么 at() 用 CHECK 而不是 throw

`std::map::at(key)` 越界会抛 `std::out_of_range`,这是标准库的规矩。可 `flat_map::at(key)` 越界直接 `CHECK` 失败、中止程序(flat_map.h:293/302),连商量的余地都不给。为什么这么狠?

因为越界访问通常意味着调用方的逻辑写错了——您要么该先用 `find` 查一下,要么就该有把握 key 一定在里头。这种 bug 在 release 里也得当场爆,而不是丢个异常出来让上层的 `try/catch` 糊过去,后者往往把真正的逻辑错误盖在了兜底逻辑底下。这是 Chromium 一以贯之的错误处理风格:确定的逻辑错误上 CHECK,不靠异常兜。[WeakPtr 的 `operator*` 用 CHECK 守失效解引用](../../02_weak_ptr/full/02-1-weak-ptr-motivation-and-api-design.md)跟这儿是同一套哲学,笔者在那篇也聊过。

### 为什么默认比较器是透明的 std::less<>

`std::map` 默认 `Compare = std::less<Key>`,不透明;flat_map 默认换成 `std::less<>`,透明的(flat_map.h:192)。这一换,异构查找就开了口子——您拿 `const char*` 去 `find` 一个 `std::string` 的 map,不用再临时构造一个 `std::string` 出来。在热路径上这点临时对象的累积相当可观,详见 [pre-03](./pre-03-flat-map-comparator-and-transparent.md)。现代 C++ 的推荐默认就是透明比较器,flat_map 直接照办。

### 为什么存 pair<K,V> 而非 pair<const K,V>

底层存的是 `std::vector<std::pair<Key, Mapped>>`,key 是非 const 的(flat_map.h:193)。这个反直觉的取舍是被 vector 的 shift 逼出来的——insert/erase 要搬迁整对元素,得能对整对做移动赋值,可 `pair<const K, V>` 偏偏不可 move-assign。代价是 key 暴露成可改的,理论上迭代器能把 key 改坏、把有序不变量打破,只能靠用户自律。这笔账的明细在 [pre-05](./pre-05-flat-map-enua-ebo-and-pair-storage.md)。

### 为什么提供 sorted_unique 构造

要是您能保证数据已经有序,用 `sorted_unique` 标签构造可以把 `O(N log N)` 的排序直接跳掉,降到 `O(N)`。更妙的是 debug 下还有 `DCHECK` 替您校验有没有撒谎——您说有序了,它真去查一遍。这是个正儿八经的零成本抽象,详见 [pre-04](./pre-04-flat-map-tag-dispatch-and-sorted-unique.md)。

---

## 咱们的实现与 Chromium 的取舍

跟前两个系列一样,咱们的教学版保留核心机制(flat_tree 适配器 + sorted vector + sorted_unique + 透明比较),做些力所能及的简化。取舍先打个预告,03-6 会拿实测对比收尾:

| 维度 | Chromium 实现 | 我们的教学版 |
|---|---|---|
| 底层 Container | `std::vector` | 同 |
| 排序 | `std::stable_sort` + unique + erase | 同 |
| 透明比较 | `KeyT<K>` + `KeyValueCompare` 两重载 | 简化为直接模板 |
| `DCHECK(is_sorted_and_unique)` | 完整 | 用 `assert` 模拟 |
| `[[no_unique_address]]` 比较器 | 标注 | 标注(GCC/Clang 都支持) |
| `replace`/`extract` | 完整 | 省略(留作扩展) |

咱们用纯标准库(`std::vector`、`std::sort`、`std::lower_bound`)把核心撸出来,Chromium 的设计哲学照搬,但那些 Chromium 特化的 `raw_ptr_exclusion`、`NO_UNIQUE_ADDRESS` 宏的复杂度全砍掉——那些是人家工程化里的私货,教学版用不着。

---

## 环境搭建

flat_map 要用到 C++20 的 concepts(`requires`、`std::convertible_to`)、ranges(`std::ranges::lower_bound`),还有 `[[no_unique_address]]` 这个属性。所以最低门槛是 C++20。

### 编译器要求

GCC 11+ 或 Clang 12+ 都行,编译加 `-std=c++20`。`[[no_unique_address]]` 在 GCC 和 Clang 上都支持,对空类型的 EBO 行为等价正确,这点您可以放心。

### 验证代码

```cpp
#include <concepts>
#include <ranges>
#include <vector>

static_assert(__cpp_lib_ranges >= 201911L);   // ranges 可用

constexpr bool check_nua_works() {
    struct Empty {};
    struct H { [[no_unique_address]] Empty e; int i; };
    return sizeof(H) == sizeof(int);   // EBO 把 Empty 折叠掉
}
static_assert(check_nua_works());
```

这段能在您机器上过,环境就算齐活了。配套工程脚手架还是用 `code/volumn_codes/vol9/full_tutorial_codes/chrome_design/`,咱们从 03-2 开始往里加 `19_`~`22_` 这一批 flat_map 示例。

动机和 API 到这儿算是理顺了。可纸面理顺是一回事,真把 flat_tree 一行行撸出来,sorted vector 适配器怎么搭、有序不变量怎么守、key 提取器怎么塞进去,全是下一篇要踩的坑。咱们下一篇就动手。

## 参考资源

- [Chromium `base/containers/flat_map.h`](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/flat_map.h)
- [Chromium `base/containers/flat_tree.h`](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/flat_tree.h)
- [Chromium `base/containers/README.md` —— 容器选择指南](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/README.md)
- [flat_map 前置知识（零）：有序关联容器与 std::map 的红黑树](./pre-00-flat-map-ordered-assoc-container-intro.md)
