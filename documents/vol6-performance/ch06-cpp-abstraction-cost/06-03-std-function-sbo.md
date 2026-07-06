---
chapter: 6
cpp_standard:
- 17
description: std::function 类型擦除,能装任何可调用对象,代价是调用间接 + 可能堆分配。本篇实测:调用比直接 lambda 慢约 6 倍(1.61
  ns vs 0.25 ns),构造时小捕获走 SBO(2.3 ns)而大捕获触发堆分配(19.6 ns,8.5 倍)。热路径上反复构造 function + 大捕获要当心,对策是模板参数(编译期多态)或固定签名函数指针
difficulty: advanced
order: 3
platform: host
prerequisites:
- 虚函数与去虚拟化
- 缓存行与局部性:64 字节的最小搬运单位
reading_time_minutes: 4
related:
- C++ 抽象的成本速查表
- RVO、NRVO 与 move 的真实成本
tags:
- host
- cpp-modern
- advanced
- 优化
- std_function
title: std::function 的小缓冲区优化:类型擦除的代价
---
# std::function 的小缓冲区优化:类型擦除的代价

## 类型擦除的便利与代价

`std::function` 是 C++ 里最方便的「装任何可调用对象」的容器——函数指针、lambda、函数对象、bind 表达式,只要签名匹配都能塞进去。它的实现靠**类型擦除(type erasure)**:不在模板参数里写死可调用对象的类型,而是在内部用一套统一的接口(通常是一个虚函数或函数指针表)来调用。

这份便利有代价。我们测一下(本机):

```text
===== std::function SBO =====
调用:
  调用 - 函数指针:               0.26 ns
  调用 - function+小lambda(SBO): 1.61 ns
  调用 - 直接 lambda(对照):     0.25 ns

构造 1000000 次:
  function 装函数指针:     2.0 ns/次
  function 装小lambda(SBO): 2.3 ns/次
  function 装大lambda(堆分配): 19.6 ns/次 ← 堆分配开销

sizeof(std::function<int(int)>) = 32
```

两个代价:

**1. 调用是间接的,比直接 lambda 慢约 6 倍**。直接 lambda(0.25 ns)和函数指针(0.26 ns)一样快(都是直接调用 + 可内联);`std::function`(1.61 ns)要走类型擦除的间接调用(查 vtable/函数指针 + 跳转),**约 6 倍**。和 06-01 的虚函数一个道理——间接调用阻碍内联。

**2. 构造可能堆分配**。`std::function` 装一个可调用对象时,要把对象的状态存起来。多数实现有 **SBO(Small Buffer Optimization,小缓冲区优化)**:在 `std::function` 对象内部预留一小块缓冲(本机 libstdc++ 的 `std::function<int(int)>` 是 32 字节),捕获状态小的(≤ SBO 阈值,通常 16-24 字节)直接存内部,不堆分配;捕获状态大的(超阈值)就只能 `new` 一块堆。

实测构造代价:小 lambda(SBO 命中)2.3 ns/次,**大 lambda(超 SBO,堆分配)19.6 ns/次**——**8.5 倍**。这个差距主要来自 `new`/`delete` 一次堆分配的成本(几十纳秒级)。

## 这两个代价什么时候咬人

**调用的 6 倍代价**:对「偶尔调用」的回调无所谓(回调不在热路径);对「每帧调用百万次」的回调,6 倍就是真金白银。比如一个事件分发器,如果每次分发走 `std::function`,调用开销可能成为瓶颈——换成模板(编译期多态)或函数指针就快得多。

**构造的堆分配代价**:这是更容易踩的坑。考虑这种代码:

```cpp
// 热路径里反复构造 function + 大捕获
for (auto& item : items) {
    std::function<void(int)> f = [item, ctx](int x) { /* 大捕获 */ };
    dispatch(f);
}
```

每次循环都构造一个 `std::function`,如果捕获大(超 SBO),**每次都 `new`/`delete`**——堆分配 + cache miss + 可能触发 malloc 锁竞争(多线程下)。这种「热路径反复构造 function」是性能黑洞,几个常见对策:

- **用模板参数(编译期多态)**:把回调类型写成模板参数,消除类型擦除。代价是调用点要编译期知道类型。
- **固定签名函数指针**:如果回调没捕获,直接用 `void(*)(int)`,零开销。
- **复用 `std::function` 对象**:在循环外构造一次,循环内只改它的状态(但改状态可能还是堆分配)。
- **避免不必要的捕获**:lambda 捕获越少越可能命中 SBO。

## SBO 与 string 的 SSO 是一回事

SBO 的思想和 `std::string` 的 **SSO(Small String Optimization)** 是一回事(都在对象内部留小缓冲,小就走内联、大才堆分配)。两者都解决的是「类型擦除/动态大小 + 避免热路径堆分配」的矛盾。SSO/SSO 的机制(为什么阈值是 16-24 字节、怎么和 ABI 配合)归 vol3/vol4;vol6 只讲「它影响热路径构造的堆分配成本」这层。

`std::function` 的 sizeof 因实现而异(libstdc++ 32 字节、libc++ 48 字节、MSFC 又不同),SBO 阈值也随之不同。所以「我这个 lambda 会不会触发堆分配」要 `sizeof` 或看实现——但**通用建议是:别在热路径依赖 SBO 命中,大捕获该换模板**。

## 小结

- **`std::function` 两个代价**:调用间接(比直接 lambda 慢 ~6 倍)、构造可能堆分配(大捕获触发,比 SBO 贵 ~8.5 倍)。
- **SBO**:小捕获(≤16-24B)存对象内部不堆分配,大捕获堆分配。
- **热路径上避免**:反复构造 `std::function` + 大捕获 = 堆分配黑洞。对策:模板参数、函数指针、复用对象、减少捕获。
- SBO 与 string SSO 同思想;机制归 vol3/vol4。

## 参考资源

- cppreference *std::function*——类型擦除语义、SBO 说明
- Stepov/Stroustrup CppCoreGuidelines *F.50*——什么时候用 function vs 模板 vs 函数指针
- Agner Fog《Optimizing software in C++》对象/容器开销。本地
- 本篇实测代码:`code/volumn_codes/vol6-performance/ch06/function_sbo.cpp`
