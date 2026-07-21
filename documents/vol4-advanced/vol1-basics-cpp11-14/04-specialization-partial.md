---
chapter: 12
cpp_standard:
- 11
- 14
- 17
description: 特化是模板最有表达力的部分。讲清全特化与偏特化的区别、编译器选「最特化」版本的优先级规则,以及两个经典应用:标准库 vector<bool>
  为什么是偏特化、type_traits 整套 is_pointer/is_const 是怎么靠偏特化搭起来的
difficulty: intermediate
order: 4
platform: host
prerequisites:
- 类模板:成员、依赖名与惰性实例化
- 函数模板深化:编译模型与那个不能偏特化的坑
reading_time_minutes: 10
related:
- 非类型模板参数:从整数到 C++20 的浮点与类类型
- 名字查找与 ADL:两阶段查找是怎么回事
tags:
- host
- cpp-modern
- intermediate
- 模板
- 泛型
title: 模板特化与偏特化:模式匹配的艺术
---
# 模板特化与偏特化:模式匹配的艺术

模板最厉害的地方,不是「写一份套多种类型」,而是**给某些特定的类型或某一类类型,单独写一份不一样的实现**。这个机制叫特化(specialization)。特化分两种:全特化针对一个具体类型,偏特化针对一类类型模式。上一篇咱们提过函数模板不能偏特化,只有类模板和变量模板能偏特化,这一篇就把类模板的特化彻底讲透:全特化、偏特化、编译器选哪个版本的优先级规则,以及两个把偏特化用到极致的经典案例,标准库的 `std::vector<bool>` 和整个 `<type_traits>`。

## 全特化:给一个具体类型单独写一份

全特化(full specialization)是针对某一个**完全确定**的类型,提供一个专门的实现。语法是 `template<>` 开头,所有模板参数都钉死。

```cpp
// 主模板
template <typename T>
struct TypeSize {
    static constexpr std::size_t value = sizeof(T);
};

// 全特化:针对 int 单独写一份
template <>
struct TypeSize<int> {
    static constexpr std::size_t value = 4;   // 假设 32 位平台 int 是 4 字节
};
```

有了这个全特化,`TypeSize<int>::value` 取的就是全特化里的 `4`,而 `TypeSize<double>::value` 走主模板算 `sizeof(double)`。全特化相当于「插队」:在编译器本该走主模板实例化 `TypeSize<int>` 的时候,看到有这么一个现成的全特化,就直接用全特化,不再生成主模板的版本。

全特化有两个特点要记住。第一,全特化**不再是模板**,它是一个具体的类(或函数、变量),所有模板参数都已确定。这影响 ODR:全特化的定义只能出现在一个翻译单元里,否则重复定义。第二,全特化的签名必须和主模板的某个实例化精确对应,差一点都不行。

## 偏特化:给一类类型模式单独写一份

偏特化(partial specialization)针对的不是一个具体类型,而是**一类类型模式**。最常见的模式是「指针类型」「引用类型」。偏特化的语法是 `template <typename T>` 开头(注意它仍然是个模板,还有未绑定的 `T`),然后类名后面用 `<T*>` 这种带模式的写法。

```cpp
// 主模板
template <typename T>
struct TypeKind {
    static const char* name() { return "generic (primary)"; }
};

// 偏特化:指针类型
template <typename T>
struct TypeKind<T*> {
    static const char* name() { return "pointer (partial)"; }
};

// 偏特化:左值引用类型
template <typename T>
struct TypeKind<T&> {
    static const char* name() { return "lvalue reference (partial)"; }
};
```

注意偏特化和全特化的关键区别:偏特化的 `template <typename T>` 头里还有一个 `T`,它仍然是模板;而全特化是 `template<>` 空,所有参数都钉死。这个区别决定了偏特化能匹配「一类类型」,全特化只能匹配「一个类型」。

跑一下看它怎么选:

```cpp
int main() {
    std::cout << "double      : " << TypeKind<double>::name() << "\n";
    std::cout << "double*     : " << TypeKind<double*>::name() << "\n";
    std::cout << "double&     : " << TypeKind<double&>::name() << "\n";
    return 0;
}
```

```bash
$ g++ -Wall -Wextra -std=c++20 spec_test.cpp -o spec_test && ./spec_test
double      : generic (primary)
double*     : pointer (partial)
double&     : lvalue reference (partial)
```

(这里还没定义 `int*` 的全特化,所以 main 暂时不碰 `int*`;等下一节加上全特化,再看 `int*` 怎么选。)

`double` 不匹配任何偏特化,走主模板;`double*` 匹配 `T*` 偏特化;`double&` 匹配 `T&` 偏特化。这是偏特化的核心能力:**根据类型的「形状」分流到不同的实现**,像一个编译期的类型 switch。

## 优先级:全特化 > 偏特化 > 主模板

上面那个 `int*` 的输出值得单独看。`int*` 既匹配 `T*` 偏特化(令 `T=int`),也匹配下面这个全特化:

```cpp
// 全特化:专门针对 int*
template <>
struct TypeKind<int*> {
    static const char* name() { return "int* (full, wins over partial)"; }
};
```

两个都能匹配,编译器选哪个?规则是**选更特化的那个**(more specialized)。全特化比偏特化更特化(它没有未绑定的参数),偏特化比主模板更特化。所以优先级是:**全特化 > 偏特化 > 主模板**。把上面的全特化定义加进去,再让 main 多打一行 `TypeKind<int*>::name()`,输出就是 `int* (full, wins over partial)`——`int*` 命中全特化,而不是走 `T*` 偏特化。

如果有多个偏特化都能匹配,规则会变得微妙一点,编译器要比谁能「更具体」。比如 `T*` 和 `T const*` 两个偏特化,对 `const int*`,`T const*` 更具体,胜出。这套「更特化」的判定有一套形式化规则,平时写代码记住「编译器会选最贴合的那个」就够了,真碰到歧义编译器会报 ambiguous,您照着调整偏特化的模式就行。

## 偏特化能匹配哪些模式

偏特化的表达力在于它能匹配的「模式」很丰富。常见的有:

- `T*`:指针类型
- `T&` / `T&&`:引用类型
- `const T*`、`volatile T`:带 cv 限定符的
- `T[N]`:数组类型
- `Foo<T>`、`Bar<T, U>`:某个具体模板的实例化
- 甚至「模板模板参数」也能参与模式匹配

把这些组合起来,偏特化几乎能识别任何「类型形状」。这正是 `<type_traits>` 能提供那么多类型查询的底层支撑,后面会看到。

## 经典应用一:std::vector\<bool\> 的偏特化

标准库里最出名的偏特化,是 `std::vector<bool>`。它不是 `std::vector<T>` 用 `T=bool` 实例化出来的普通版本,而是库专门写的一个偏特化。目的是让一个 `bool` 只占一个 bit,而不是一个字节,省内存。

```cpp
// 标准库内部大致是这个意思(简化示意)
template <typename T, typename Alloc = std::allocator<T>>
class vector { /* 普通实现,一个 T 一份存储 */ };

template <typename Alloc>
class vector<bool, Alloc> { /* 偏特化:位压缩,一个 bool 一个 bit */ };
```

`vector<bool>` 的偏特化有个肉眼可见的副作用:它的 `operator[]` 返回的不是 `bool&`,而是一个**代理对象**(proxy object)`std::vector<bool>::reference`。原因是位压缩后,您没法返回「一个 bit 的引用」(内存最小寻址单位是字节),只能返回一个模拟 `bool&` 行为的临时对象。跑一下能直接看到这个差异:

```cpp
#include <iostream>
#include <type_traits>
#include <vector>

int main() {
    std::cout << std::boolalpha;
    // vector<bool> 的 reference 是代理类,不是 bool&
    std::cout << "vector<bool>::reference is bool&?   "
              << std::is_same_v<std::vector<bool>::reference, bool&> << "\n";
    // 普通 vector 的 reference 就是元素引用
    std::cout << "vector<char>::reference is char&?  "
              << std::is_same_v<std::vector<char>::reference, char&> << "\n";
}
```

```bash
$ g++ -Wall -Wextra -std=c++20 vecbool.cpp -o vecbool && ./vecbool
vector<bool>::reference is bool&?   false
vector<char>::reference is char&?  true
```

这个代理 `reference` 给 `vector<bool>` 惹了不少争议。它让 `vector<bool>` 不完全满足「序列容器」的要求(因为 `reference` 不是真正的元素引用),也让一些通用代码在它上面行为异常,比如 `auto& x = vec[0];` 在 `vector<bool>` 上**直接编不过**(代理 `reference` 是个右值临时对象,绑不了非 const 左值引用);就算改成 `auto x = vec[0];`,拿到的也是代理对象、不是 `bool`,某些操作会出乎意料。委员会多次讨论要不要把它移出标准、改成 `dynamic_bitset` 之类的独立设施,但因为它已经到处在用,改动代价太大,就这么一直留着。这是偏特化的一个「反面教材」:偏特化可以完全重新定义实现,但代价是它可能不再满足主模板隐含的接口契约,使用者会踩坑。

## 经典应用二:type_traits 的整套套路

`<type_traits>` 里的 `std::is_pointer`、`std::is_const`、`std::is_reference` 这些查询,底层全是「主模板返回 false + 偏特化命中返回 true」的套路。咱们手写一个 `is_pointer` 就能看穿它:

```cpp
// 主模板:默认不是指针
template <typename T>
struct is_pointer {
    static constexpr bool value = false;
};

// 偏特化:指针类型才命中
template <typename T>
struct is_pointer<T*> {
    static constexpr bool value = true;
};
```

跑一下:

```bash
$ g++ -Wall -Wextra -std=c++20 is_ptr.cpp -o is_ptr && ./is_ptr
is_pointer<int>::value       = false
is_pointer<int*>::value      = true
is_pointer<int**>::value     = true
is_pointer<int&>::value      = false
```

`int` 走主模板,`value=false`;`int*` 命中 `T*` 偏特化,`value=true`;`int**` 也命中(`T=int*`);`int&` 是引用不是指针,走主模板,`false`。

这就是 `std::is_pointer` 的全部秘密。标准库的 `is_pointer` 比这个多一点(还要处理指向成员的指针、cv 限定等边界),但核心思路就是这个主模板+偏特化的 pattern。`is_const`(`const T*` 不算,`T const` 算)、`is_reference`(`T&` 和 `T&&`)、`is_array`(`T[N]`)全是同一套办法:主模板给个默认值,偏特化针对目标模式覆盖。

理解了这个套路,您就能自己写类型查询了。比如想查「是不是函数指针」,就写主模板默认 false,偏特化 `R(*)(Args...)` 命中 true。整个 `<type_traits>` 就是几百个这样的偏特化堆出来的。后面第二部分讲 SFINAE,第三部分讲 concepts,都建立在您理解「靠偏特化做编译期类型判断」这件事之上。

## 偏特化的几条限制

最后把偏特化的边界讲清楚,免得踩坑。

**函数模板不能偏特化**。这条上一篇专门讲过,标准只允许类模板和变量模板(C++14)偏特化。函数要分流走重载、走 `if constexpr`、走 SFINAE。

**偏特化通常写在命名空间作用域**。绝大多数偏特化都放在命名空间级别。C++11 之后(CWG 727)成员类模板的偏特化也可以出现在外层类的作用域里,但写法绕、用得少,平时就把偏特化放命名空间,省心。

**偏特化可以完全重新定义实现**。偏特化不必和主模板有相同的成员,它可以彻底长一个样。但这也意味着,使用者通过主模板名字访问时,得保证偏特化提供了对应的成员,否则换个类型就编不过。`vector<bool>` 就是这么干的,它的成员和普通 `vector` 几乎一样但语义有微妙差别。

**偏特化和全特化的 ODR 不同**。偏特化仍然是模板,它的实例化在多个翻译单元里会自动合并,不违反 ODR。全特化是具体实体,定义只能在一个翻译单元,或者标 `inline`。

下一篇咱们进非类型模板参数。偏特化的模式匹配里,非类型参数是个重要角色,`std::array<T, N>` 的 `N` 就是它,而 C++20 又把非类型参数能接受的类型大幅放宽了,从只能用整数和指针,扩展到浮点甚至满足条件的类类型。
