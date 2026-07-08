---
chapter: 1
cpp_standard:
- 11
- 17
- 20
description: 讲清 NoDestructor "故意泄漏"的权衡——LeakSanitizer 会误报,Chromium 用 storage_ptr_
  reachability hack 兼容 LSan(crbug/40562930)
difficulty: intermediate
order: 4
platform: host
prerequisites:
- NoDestructor 实战（二）：核心实现
- NoDestructor 实战（三）：何时用、何时不用
reading_time_minutes: 8
related:
- NoDestructor 设计指南（一）：动机与实现
tags:
- host
- cpp-modern
- intermediate
- 内存管理
- 内存安全
title: "NoDestructor 实战（四）：LSan 泄漏权衡与 reachability hack"
---
# NoDestructor 实战（四）：LSan 泄漏权衡与 reachability hack

[04-2](./04-2-no-destructor-core-impl.md) 里咱们把 NoDestructor 的核心拆成了"placement new + 不调 `~T()`"。笔者当时写完那段,心里其实还堵着一件事:T 持有的那些堆资源——比如 `NoDestructor<vector<int>>` 里 vector 自己在堆上分配的元素——既然 `~T()` 不跑了,它们就永远没人释放,一路"泄漏"到进程结束。对一个活到程序最后的全局单例,这其实无所谓,进程都退了 OS 自然会把整块进程内存收回去。可问题是,在 LeakSanitizer(LSan)眼里,这玩意儿就是货真价实的"内存泄漏",会拉响告警。

这就把咱们逼到了一个权衡面前。笔者先把代价摆清楚。

T 的析构平时其实干两件事:一是把 T 自己持有的资源还回去——vector 的堆内存、文件的 close 都算;二是跑一些"对外有影响"的副作用,比如把日志 flush 到磁盘、给别的进程发个通知。NoDestructor 跳过 `~T()`,这两件事就都断了。第一件无所谓,反正 OS 兜底;第二件才是真要小心的——您要是拿它包一个析构必须 flush 日志的类型,数据就丢了。所以一条比较硬的边界:NoDestructor 适合"析构只是还资源"的类型,不适合"析构有外部副作用"的类型。

不过这代价换来的是笔者觉得最值钱的东西:没有析构顺序问题。关停竞态这事儿,在 Chromium 这种关停路径绕得要命的大型程序里,谁踩过谁知道——能整个绕开,资源那点"不释放"完全能接受,LSan 告警嘛,咱们下面就来收拾它。

---

## LeakSanitizer 怎么工作

要理解为什么会告警,咱们得先看一眼 LSan 是怎么判定泄漏的。它是 AddressSanitizer 家族里专门管泄漏的那位,干的事叫**可达性分析(reachability analysis)**——名字唬人,思路其实朴素:程序退出时,它先把所有"根"摸一遍,根就是全局变量、栈上和寄存器里的那些指针;然后从这些根出发,顺着指针一路追,凡是追得到的堆内存都算"可达";剩下那些没有任何活指针能找到的堆块,就是泄漏。

注意这里 LSan 完全不关心"这块内存该不该析构"——它只看一件事:这块内存还有没有人指着。没人指,就判泄漏,哪怕您心里清楚这是"故意泄漏 + OS 会兜底"的无害情况。

---

## NoDestructor 在 LSan 视角下的问题

来看这段最朴素的代码:

```cpp
static const base::NoDestructor<std::vector<int>> v({1, 2, 3});
```

`v` 内部是 `alignas(vector) char storage_[sizeof(vector)]`,一段 char 数组,vector 是 placement new 上去的。vector 自己又在堆上分配了 `{1,2,3}` 那块存储。布局没问题,人看着也没问题。

LSan 扫根的时候,确实会看到 `v` 的 `storage_`——可关键是,`storage_` 的类型是 **char 数组**。LSan 不会把它当作"指向 vector 堆内存的指针"来追,在它眼里那就是一坨原始字节,哪怕那几个字节恰好就是 vector 内部的指针。这一步就断了:vector 在堆上分配的 `{1,2,3}`,从 LSan 的根集出发谁也摸不着它——可达性分析走到这儿就死胡同,自然判泄漏。

这就是 [crbug.com/40562930](https://crbug.com/40562930) 记的那个坑:`NoDestructor<vector<int>>` 在 LSan 构建下会被误报泄漏。说是"误报",其实 LSan 没判错——它的规则就是看可达性,可达性在这儿确实断了;只是从工程角度,咱们知道这是无害的故意泄漏。

---

## reachability hack:storage_ptr_

Chromium 的 workaround,笔者第一次读到的时候愣了一下——是真没想到还能这么干(no_destructor.h:132-142):

```cpp
#if defined(LEAK_SANITIZER)
    // TODO(crbug.com/40562930): This is a hack to work around the fact
    // that LSan doesn't seem to treat NoDestructor as a root for reachability
    // analysis. ...
    // hold an explicit pointer to the placement-new'd object in leak sanitizer
    // mode to help LSan realize that objects allocated by the contained type
    // are still reachable.
    T* storage_ptr_ = reinterpret_cast<T*>(storage_);
#endif
```

在 LSan 构建下,它额外塞了一个 `T* storage_ptr_` 成员进去,指向 placement new 上去的那个 T 对象。说白了,这地址跟 `storage_` 是同一个,只不过类型从 `char*` 换成了 `T*`。

换这一个类型,问题就解了。前面咱们卡在哪?卡在 LSan 把 `storage_` 当原始字节看,里头的指针它不认。可 `storage_ptr_` 的类型是 **`T*`**——这一下 LSan 就把它当成正经的"指向 T 对象的指针根"了。从它出发,LSan 能追到 T 对象,再顺着 T 对象里的指针,一路追到 vector 在堆上的 `{1,2,3}`。整条可达链就这么接上了,那些内存重新变成"可达",告警消失。

说白了就是这么个手法:char 数组里藏着的指针 LSan 看不懂,那咱们就再给它一个它看得懂的、显式的 `T*`——把 placement new 上去的对象在 LSan 视角下"显式化"一遍。地址没变,对象没变,变的只是"LSan 能不能识别这个根"。

还有个细节值得点一下:这个成员只在 `LEAK_SANITIZER` 定义时才存在,普通构建里压根没这玩意儿,零开销。条件编译的标准做法——只为需要它的构建(开了 LSan 的测试/调试构建)付这点代价。

---

## 教学版的简化

咱们的教学版把这个 LSan hack 省掉了(实现见 [04-2](./04-2-no-destructor-core-impl.md))。理由笔者想了想,有这么几条。

它只在开了 LSan 的构建里才有影响,普通编译根本碰不到。而且它本质上是"让工具别误报"的工程细节,不是 NoDestructor 的核心机制——核心还是 placement new 加上不析构那一套,这个 hack 只是外围补丁。更实际的是,如果您的工程真要在 LSan 下跑干净,不一定非得改代码:LSan 自己支持 suppression 文件(比如 `lsan_suppressions.txt`),在里面把 NoDestructor 的那块泄漏显式忽略掉,效果一样。

当然,如果您就是想让代码在 LSan 构建下零告警、不想维护 suppression 文件,照 Chromium 加上那一行 `#if defined(LEAK_SANITIZER) T* storage_ptr_; #endif` 也完全没问题——它就是个条件编译,加或不加都不影响 NoDestructor 的语义,纯粹是给 LSan 看的。

---

到这里,NoDestructor 这条线——动机、实现、什么时候用、LSan 兼容——就讲完整了。回头看它的全貌其实挺有意思:就用 placement new 加上不析构这么点代码,换来了"全局/静态对象没有析构顺序问题"这个 C++ 老大难里相当实在的一块解法,顺手还把 LSan 那个可达性的坑用一行 `T*` 补上了。在 vol9/chrome 这个系列里,它跟 OnceCallback(回调)、WeakPtr(弱引用)、flat_map(容器)搭在一起,正好凑成 Chromium `//base` 那套基础设施的四块——回调、弱引用、容器、静态生命周期,各管一摊。

## 参考资源

- [Chromium `base/no_destructor.h` —— LSan hack 注释(no_destructor.h:132-142)](https://source.chromium.org/chromium/chromium/src/+/main:base/no_destructor.h)
- [crbug.com/40562930 —— NoDestructor LSan 误报](https://crbug.com/40562930)
- [LeakSanitizer 文档](https://clang.llvm.org/docs/LeakSanitizer.html)
- [AddressSanitizer 家族](https://clang.llvm.org/docs/AddressSanitizer.html)
- [NoDestructor 实战（二）：核心实现](./04-2-no-destructor-core-impl.md)
