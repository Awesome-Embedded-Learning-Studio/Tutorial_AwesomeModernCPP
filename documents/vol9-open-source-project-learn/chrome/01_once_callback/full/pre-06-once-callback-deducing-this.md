---
chapter: 0
cpp_standard:
- 23
description: 深入理解 C++23 显式对象参数（deducing this）如何让 OnceCallback::run() 在编译期优雅地拦截左值调用，替代
  Chromium 的双重重载 hack
difficulty: intermediate
order: 6
platform: host
prerequisites:
- OnceCallback 前置知识速查：C++11/14/17 核心特性回顾
reading_time_minutes: 8
related:
- OnceCallback 实战（二）：核心骨架搭建
- OnceCallback 前置知识（四）：Concepts 与 requires 约束
tags:
- host
- cpp-modern
- intermediate
- 模板
title: OnceCallback 前置知识（六）：Deducing this (C++23)
---
# OnceCallback 前置知识（六）：Deducing this (C++23)

## 先看一眼那行声明

OnceCallback 的 `run()` 是整个组件里最反直觉的一个方法，也是 C++23 特性最密集的地方。它的声明长这样：

```cpp
template<typename Self>
auto run(this Self&& self, FuncArgs&&... args) -> ReturnType;
```

`this Self&& self` 这写法笔者头回见的时候愣了两秒——成员函数还能把 `this` 显式写成参数？这是 C++23 的「显式对象参数」，官方名字叫 deducing this。别看就这么一行，它让 OnceCallback 拿一个函数模板就把「左值调用编译报错、右值调用正常跑」这件事给办了，比 Chromium 那套干净一大截。咱们这篇就把这玩意儿拆透——语法、推导规则、和 OnceCallback 怎么借它做编译期拦截。

## 先把问题摆清楚:凭什么 `cb.run()` 不能编过

OnceCallback 的核心语义就一句——「只能调一次,而且必须右值调」。翻译成代码:

```cpp
OnceCallback<int(int)> cb([](int x) { return x * 2; });

cb.run(5);                  // 应该编译失败：cb 是左值
std::move(cb).run(5);       // 应该编译通过：std::move(cb) 是右值
```

咱们要的就是个编译期分流的机制:左值调,直接红给您看,错误信息还得说人话;右值调,放行。

### Chromium 没 C++23,只好写两个重载

Chromium 那会儿没 C++23 可用,只能上 hack——同一件事写两份重载:

```cpp
// 右值版本：真正的执行
R Run() && {
    // 执行回调...
}

// 左值版本：编译报错
R Run() const& {
    static_assert(!sizeof(*this),
        "OnceCallback::Run() may only be invoked on a non-const rvalue, "
        "i.e. std::move(callback).Run().");
}
```

这里有个细节笔者当初卡了一阵:为啥不直接 `static_assert(false, "...")`?因为在 C++23 之前,模板里写死 `false` 会无差别触发——哪怕这个重载一辈子没被调用过,编译器在模板定义点就给您炸了。`!sizeof(*this)` 是个绕路:它依赖 `*this` 的类型,是依赖型表达式,得等到模板实例化那一刻才求值。换句话说,真有人写了 `cb.Run()` 才炸,没写就当它不存在。

能跑,真不优雅。两份重载干一件事不说,`!sizeof` 这个 hack 读起来还得反应两秒。C++23 把 deducing this 给落地之后,这事儿就有正经解法了。

---

## deducing this:把 `this` 写成参数

deducing this 干的事就一句——把原本隐式藏在成员函数里的 `this`,拎出来显式写成第一个参数,顺带给它套个模板推导。

### 语法

```cpp
struct MyStruct {
    void f(this auto&& self) {
        // self 就是 this——但它的类型是推导出来的
    }
};
```

`this` 关键字往类型前头一搁,等于跟编译器打招呼:后头这玩意儿不是普通参数,是显式的对象参数。`auto&&` 是推导占位符,谁调它、拿啥调它,推导出来的类型就长啥样。

### 推导规则:跟转发引用一模一样

`self` 的推导规则,跟咱们写模板时碰到的转发引用(forwarding reference)完全是一个模子——因为 `self` 的推导上下文等效于一个模板参数。这点很关键,后面拦截左值就靠它。

拿 `obj.f()` 这种左值调来说,`self` 推成 `MyStruct&`,左值引用。换 `std::move(obj).f()` 或者 `MyStruct{}.f()` 这种右值调,`self` 推成 `MyStruct`,纯类型、没引用。再上 `std::as_const(obj).f()` 这种 const 左值调,`self` 就老老实实推成 `const MyStruct&`。记不住没关系,跑一下就出来了。

### 跑一下验证

```cpp
#include <iostream>
#include <type_traits>

struct Check {
    void test(this auto&& self) {
        using Self = decltype(self);
        if constexpr (std::is_lvalue_reference_v<Self>) {
            std::cout << "lvalue reference\n";
        } else {
            std::cout << "rvalue (not a reference)\n";
        }
    }
};

int main() {
    Check c;
    c.test();                  // 输出：lvalue reference
    std::move(c).test();       // 输出：rvalue (not a reference)
    std::as_const(c).test();   // 输出：lvalue reference (const)
}
```

---

## 落到 `run()` 上:deducing this 怎么干活的

看完语法,咱们直接上 `run()` 的完整实现,看它怎么把左值调用摁在编译期。

```cpp
template<typename Self>
auto run(this Self&& self, FuncArgs&&... args) -> ReturnType {
    static_assert(!std::is_lvalue_reference_v<Self>,
        "OnceCallback::run() must be called on an rvalue. "
        "Use std::move(cb).run(...) instead.");
    return std::forward<Self>(self).impl_run(std::forward<FuncArgs>(args)...);
}
```

这几行里塞了三个互相咬合的机关,咱们挨个拆。

### 拦截左值调用

`std::is_lvalue_reference_v<Self>` 在问 `Self` 是不是左值引用。调用方写 `cb.run(args)`,`cb` 是左值,`Self` 推成 `OnceCallback&`,左值引用,`is_lvalue_reference_v` 返回 `true`,取反变 `false`,`static_assert` 当场炸,顺手把笔者写的那句人话错误信息甩给调用方:`OnceCallback::run() must be called on an rvalue. Use std::move(cb).run(...) instead.`

调换一下,写 `std::move(cb).run(args)`,`std::move(cb)` 是右值(严格说是 xvalue),`Self` 推成 `OnceCallback`,非引用,`is_lvalue_reference_v` 返回 `false`,取反变 `true`,断言过,代码继续往下走。一推一拉,左值右值的命运就在编译期分了叉。

### 转发给 impl_run

过了断言这关,`std::forward<Self>(self)` 把 `self` 原样转给真正的执行函数 `impl_run`。因为 `static_assert` 已经把左值的路堵死了,能走到这一步的 `Self` 一定是非引用的右值,`std::forward<Self>(self)` 就老实地返回右值引用,保证 `impl_run` 拿到的是个右值。这步看着不起眼,但它是「拦截」和「执行」之间那根接力棒,缺了它语义就漏了。

### 顺带说一句惰性实例化

这里头有个笔者玩味了挺久的细节——`static_assert` 的条件挂着模板参数 `Self`,所以它只在模板实例化那一刻才求值。反过来说,如果 `run()` 压根没人调,这个 `static_assert` 就一直趴着不动,不管那个 `OnceCallback` 对象本身是左值还是右值都跟它无关。只有真有人在某一行写了 `cb.run(...)`,编译器非得实例化这个模板不可的时候,`Self` 的具体类型才定下来,断言才肯抬眼算一下。

这就是模板的惰性实例化(lazy instantiation)——函数模板不使就不实例化,也不查。这也解释了为啥 Chromium 非 `!sizeof(*this)` 不可:在 C++23 之前,`static_assert(false)` 跟模板参数没关系,会在模板定义点就先炸了,根本等不到实例化。

---

## 跟传统 ref-qualifier 比,什么时候该用谁

OnceCallback 里有两个方法都表达了「只能右值调」这层意思——`run()` 用 deducing this,`then()` 用传统的 ref-qualifier `&&`。笔者一开始也纳闷,既然 deducing this 这么好,干嘛不一刀切全用它?后来把两个场景的需求摆在一起比,才想通——它们要的精细度根本不一样。

### then() 用 ref-qualifier 就够了

```cpp
template<typename Next>
auto then(Next&& next) && -> OnceCallback<...>;
```

`then()` 这边诉求很朴素:右值照收,左值直接拒之门外,也不用费心去解释为啥拒。调用方真写了 `cb.then(next)`(左值调),编译器甩一句「没有匹配的重载」就完事。错误信息虽然糙了点,没 deducing this 那么有指导性,但够用。ref-qualifier 写起来也省事,末尾挂个 `&&`,一个字符搞定。

### run() 非要 deducing this 不可

`run()` 这边就挑剔多了。光把左值拒了不够,还得告诉调用方「您该写的是 `std::move(cb).run(...)`,不是 `cb.run(...)`」——一条能让人立刻改对人话的错误信息。deducing this 配 `static_assert` 恰好把这件事做得很自然:错误信息是笔者自己塞进去的,不是编译器那套「no matching function」的模板。

### 怎么挑

所以笔者的判断是:只图个「只收右值」的约束,`&&` 够用,简洁。要是还得对左值调用甩一句自定义的人话错误,那就上 deducing this 配 `static_assert`。工具选哪个,看您要不要解释。

---

## 踩坑预警

笔者这里栽过的坑,您顺手避开。

显式对象参数有个硬规矩——它不能跟 cv-qualifier 或 ref-qualifier 同时出现。道理也好理解:对象类型和值类别已经被显式参数接管了,您再叠个 `const` 或挂个 `&&`,编译器就懵了,这俩到底谁说了算?所以下面这写法直接编不过:

```cpp
struct Bad {
    void f(this auto&& self) const;   // 编译错误：不能同时有显式对象参数和 const
    void g(this auto&& self) &&;      // 编译错误：不能同时有显式对象参数和 &&
};
```

还有一处容易误会的:显式对象参数函数看着像静态函数,其实不是——它照样得有个对象实例才能调。`this` 参数是编译器从调用表达式里推出来的,不是调用方手动塞进去的,这点别搞反。

最后是工具链门槛。deducing this 是 C++23 特性,GCC 14+、Clang 18+、MSVC 19.34+ 才认。您要是还在用老编译器,那就只能回退到 Chromium 那套双重重载的方案了——hack 是 hack 了点,但起码能跑。

---

到这里,deducing this 这把刀就算磨完了。OnceCallback 的 `run()` 靠它一个函数模板就把左值右值在编译期分流,Chromium 那两份重载加 `!sizeof` hack 的苦日子总算到头了。`then()` 那边没这讲究,挂个 `&&` 图个省事。工具怎么挑,看您要不要给调用方甩人话错误。

前置知识到这就收尾,下一篇咱们正式动手搭 OnceCallback 的骨架。

## 参考资源

- [P0847R7 - Deducing this 提案](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2021/p0847r7.html)
- [C++23's Deducing this (Microsoft C++ Blog)](https://devblogs.microsoft.com/cppblog/cpp23-deducing-this/)
- [cppreference: Explicit object parameter](https://en.cppreference.com/w/cpp/language/member_functions#Explicit_object_parameter)
