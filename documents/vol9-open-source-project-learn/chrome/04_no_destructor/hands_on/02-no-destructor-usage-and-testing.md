---
chapter: 1
cpp_standard:
- 11
- 17
- 20
description: NoDestructor 的使用边界(该用/四个别用)、constinit 全局 vs 函数局部静态、
  LSan reachability hack、测试不变量
difficulty: advanced
order: 2
platform: host
prerequisites:
- NoDestructor 设计指南（一）：动机、接口与实现
reading_time_minutes: 6
related:
- NoDestructor 设计指南（一）：动机、接口与实现
tags:
- host
- cpp-modern
- advanced
- 内存管理
- 内存安全
title: "NoDestructor 设计指南（二）：使用边界、LSan 与测试"
---
# NoDestructor 设计指南（二）：使用边界、LSan 与测试

上一篇咱们把 NoDestructor 的"为什么"和实现抠了一遍。这一篇管"怎么用对"。笔者老实讲,这玩意儿比想象中更容易用错,因为能用它的场景其实非常窄,但每一处用错的姿势都挺自然。咱们先把唯一该用的场景立住,再倒着扫一遍四个最常见的误用,最后聊聊两个绕不开的工程细节:线程安全到底靠谁、LSan 为什么会对它发脾气。

## 该用的唯一场景

```cpp
const T& GetGlobal() {
    static const base::NoDestructor<T> x(args...);   // ✓ 函数局部静态 + 非平凡析构 T
    return *x;
}
```

这三行看着平平无奇,但三个条件一个都不能少:T 是非平凡析构的(不然用 NoDestructor 纯属多余),写成函数局部静态(这点下面单独讲),而且它得是整个程序都要用的全局性对象。三条凑齐,才轮到 NoDestructor 出场。

## 四个别用

真正该用 NoDestructor 的就上面那一种。可笔者翻自己以前的代码、看社区里的用法,错用的姿势倒有一大堆。Chromium 源码在 `no_destructor.h:15-46` 的 Caveats 里把它们列得挺清楚,咱们按踩坑频率归成四类:

| 场景 | 别用 NoDestructor,改用 | 理由 |
|---|---|---|
| 局部变量/成员 | 普通 `T` 或 `unique_ptr<T>` | NoDestructor 会**真泄漏**(该回收时不回收) |
| 平凡析构 T | 裸 `static T x` | 不产生全局析构器,无需 NoDestructor |
| 平凡可构造+析构 T | `constinit T x` / `constexpr` | 编译期初始化,无运行时代码 |
| 很少用的数据 | 按需创建函数(返回值) | NoDestructor 缓存浪费 bss 内存 |

头一条是最容易糊的:有人把 NoDestructor 当成"高级的 unique_ptr"往成员里塞,或者干脆包局部变量。这一塞不是省析构,是真把内存漏了,对象该回收的时候没人回收它。中间两条其实是同一个意思的两种说法:如果 T 析构啥也不干(POD、trivially destructible),那它压根不产生全局析构器,您再套一层 NoDestructor 等于多此一举;再进一步,要是它还能 constinit/constexpr 构造,编译期就初始化好了,连运行时代码都没有,那 constinit 才是正解。最后一条偏冷门但笔者亲测过:一些"以防万一先缓存着"的冷数据,用 NoDestructor 一挂就是整个 bss 段,不如老老实实按需造。

## constinit 全局 vs 函数局部静态

"该用的唯一场景"里笔者一直强调函数局部静态,这里把例外也讲明白。默认就老老实实写函数局部静态,它一举两得:绕开了全局构造器带来的初始化顺序坑,又借 C++11 magic statics 拿到线程安全,代码还短。只有当 T 本身能 constinit 构造的时候,您才能写全局 `constinit const NoDestructor<T> g(...)`,这样不产生任何静态初始化器,是真零开销。

这里有个笔者踩过的暗坑。T 要是不能 constexpr 构造,您别以为写个全局 `NoDestructor<T> g(...)` 就万事大吉,它照样会生成静态初始化器,因为 NoDestructor 自己的构造函数不是 constexpr 的。这种情况没别的路,必须退回函数局部静态。换句话说,constinit 全局这条路只对 constinit 可构造的 T 敞开,门槛比想象中高。

## magic statics:线程安全靠它,不靠 NoDestructor

这一点容易被"用了 NoDestructor 就线程安全了"的错觉盖过去。NoDestructor 自己一行锁都没加,它对线程安全零贡献。真正撑场子的是 C++11 函数局部静态那套初始化保证:首个线程进入函数时初始化,其他并发线程被挡住等它完成。所以前面反复强调"函数局部静态"不只是绕开 ctor 的小聪明,更是 NoDestructor 线程安全的唯一来源。换个姿势用,这条保证就没了。

## LSan 泄漏权衡

"不析构"听着爽,代价也得摊开讲。一是资源不释放,好在进程退出时 OS 会兜底回收,所以纯内存型 T 影响不大;二是析构的副作用不发生,这才是要命的:如果 T 的析构里有刷盘、通知、状态上报这类动作,您跳过它,程序逻辑就坏了。所以 NoDestructor 只适合纯资源型 T,凡是析构有副作用的,一律别碰。

然后是个挺恶心的 LSan 误报。NoDestructor 把对象塞在 `char storage_[]` 里,这个 byte 数组 LSan 看不懂,它做可达性分析时认不出里头藏着指针,于是把 NoDestructor 持有的堆内存一律判成泄漏。Chromium 的解法是个相当取巧的 hack(`no_destructor.h:132-142`):在 `LEAK_SANITIZER` 构建下,额外再持一个 `T* storage_ptr_ = reinterpret_cast<T*>(storage_)`,相当于给 LSan 喂一个它能认识的 `T*` 根,把可达链接接上(crbug/40562930)。这个字段只在 LSan 构建下存在,普通构建零开销。咱们教学版省了这一手,跑 LSan 时用 suppression 文件把误报压掉就行。

## 测试不变量

NoDestructor 的行为正确性,落到测试上其实是五条不变量。咱们一条条过。

构造只跑一次,这是 magic statics 的承诺——多次调用包裹函数,T 的构造函数应当只触发一回,可以用计数构造次数验证。紧接着是它的反面:析构永远不跑。这一条没法用普通断言验,因为析构压根不发生;常见做法是搞一个析构里打 log 的 Noisy T,程序跑完检查那个 log 没出现,death 测试或单独进程隔离跑都行。

第三条是编译期就把关的:NoDestructor 拒绝平凡析构的 T,所以 `NoDestructor<int>` 这种用法应当被 static_assert 在编译期拒掉,根本编不过。第四条线程安全,多线程并发首次调用同一个包裹函数,构造函数还是只跑一次,用计数器加多线程压一压,无竞态就算过。最后是访问语义,`*nd`、`nd->`、`nd.get()` 三个口子的行为都得跟普通 T 对得上。这属于基本盘,但笔者见过有人改了内部 storage 后漏测访问语义,翻车的,别省这几行测试。

NoDestructor 整个系列到此完结。回头看,vol9/chrome 这一路攒下来正好是四块拼图:**OnceCallback** 管回调的生命周期与取消,**WeakPtr** 管弱引用,**flat_map** 管高性能容器,**NoDestructor** 管静态生命周期。四个不同维度的工业级 C++ 设计,凑一块儿,刚好把 Chromium `//base` 里最值得学的几块都过了一遍。

## 参考资源

- [Chromium `base/no_destructor.h` —— Caveats + LSan hack](https://source.chromium.org/chromium/chromium/src/+/main:base/no_destructor.h)
- [crbug.com/40562930 —— LSan 误报](https://crbug.com/40562930)
- [LeakSanitizer 文档](https://clang.llvm.org/docs/LeakSanitizer.html)
- [NoDestructor 实战（三）：何时用、何时不用](../full/04-3-no-destructor-when-to-use.md)
