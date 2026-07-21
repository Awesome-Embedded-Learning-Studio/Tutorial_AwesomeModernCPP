---
title: "类模板:成员、依赖名与惰性实例化"
description: "类模板是 STL 的基石。这一篇讲它写起来和函数模板的三个关键差异:成员函数类内还是类外定义、惰性实例化的脾气(没用到就不报错)、依赖名为什么要 typename 和 this-> 消歧义"
chapter: 12
order: 3
tags:
  - host
  - cpp-modern
  - intermediate
  - 模板
  - 泛型
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 17]
reading_time_minutes: 20
prerequisites:
  - "模板导论:从一份代码配方说起"
  - "函数模板深化:编译模型与那个不能偏特化的坑"
related:
  - "模板特化与偏特化:模式匹配的艺术"
  - "名字查找与 ADL:两阶段查找是怎么回事"
---

# 类模板:成员、依赖名与惰性实例化

类模板是 STL 的基石,`std::vector`、`std::map`、`std::string`(其实是 `std::basic_string<char>`)全是类模板。会写函数模板不等于会写类模板,这两者有几个关键差异,正好是新手写类模板时反复栽跟头的地方。这一篇聚焦三个差异:成员函数写在类内还是类外、惰性实例化带来的「藏错」脾气、以及依赖名为什么需要 `typename` 和 `this->` 来消歧义。把这三个吃透,您写自己的容器、写策略类、读 STL 源码就不会卡在这些细节上。

## 类模板长什么样

从一个最小的栈开始,边写边看。

```cpp
template <typename T>
class Stack {
public:
    void push(const T& value) { data_.push_back(value); }
    void pop() { data_.pop_back(); }
    const T& top() const { return data_.back(); }
    bool empty() const { return data_.empty(); }
private:
    std::vector<T> data_;   // 用 vector 当底层存储
};
```

`template <typename T>` 告诉编译器这是一个类模板,`T` 是类型参数。类里凡是出现 `T` 的地方,实例化时都会被替换成具体类型。`Stack<int>` 把 `T` 全换成 `int`,`Stack<std::string>` 换成 `std::string`,各自生成一个独立的类。

上面把所有成员函数都写在类内部,这是最常见的写法,简洁。成员函数也可以写到类外面,但写法有个坑,下一节专门讲。

类模板的成员函数本身也是模板(准确地说是「被模板化的函数」,templated function)。只有当某个成员函数被实际调用时,编译器才会为当前类型实例化它。这一点引出了类模板最重要的脾气。

## 惰性实例化:没用到就不付钱

类模板的成员函数,**只有真正被用到的那几个才会被实例化**。写一个 `Stack<Heavy>` 不会把 `Stack` 所有成员函数都生成一遍,只生成您实际调用的那些。这一条上一篇提过,这里用个更直白的例子看清楚。

```cpp
#include <iostream>

template <typename T>
struct Box {
    T value;

    void used_show() const {
        std::cout << "value = " << value << "\n";
    }

    // 这个函数体里写了 T 上根本不存在的 nonexistent_field
    // 只要没人调用它,编译器就不会实例化,也就不会报错
    void unused_broken() const {
        std::cout << value.nonexistent_field << "\n";
    }
};

int main() {
    Box<int> b{42};
    b.used_show();   // 只用到这一个
    return 0;
}
```

`unused_broken` 里写的 `value.nonexistent_field`,在 `T=int` 时是 `int::nonexistent_field`,纯属胡扯。可这段代码编译通过、运行正常:

```bash
$ g++ -Wall -Wextra -std=c++20 lazy_inst.cpp -o lazy_inst && ./lazy_inst
value = 42
```

原因就是惰性实例化。`main` 里只调用了 `used_show`,编译器只为 `Box<int>::used_show` 做实例化;`unused_broken` 从头到尾没人用,编译器连看都没看它一眼,里面写什么胡话都不报错。

这条特性是 STL 的底气。`std::vector` 有几十个成员函数,您要是只用到 `push_back` 和 `size`,编译器就只实例化这两个,其它的 `insert`、`erase`、`emplace` 一概不生成。否则每种 `vector<X>` 都要把全部成员实例化一遍,编译时间和二进制体积会膨胀到不可接受。

有利就有弊。惰性实例化的代价是**错误藏得深**。您在某个成员函数里写了个类型错误,只要测试用例没覆盖到那个函数,编译期就不会暴露。等哪天有人真的调用了它,可能已经是很久以后、在很远的代码里,排查起来就费劲了。所以写类模板时,最好显式地用一个「全类型自检」把所有成员都至少实例化一遍,或者写单元测试覆盖到每个成员函数。GCC 有个 `-Wtemplate-body` 之类的诊断能帮您抓一部分这类问题,新版 GCC 默认就会在模板体内对一些明显错误给出提示。

## 类外定义成员函数:模板头不能少

成员函数写到类外面,语法是 `template <...>` 头加上 `ClassName<T>::` 限定。两个都不能少。

```cpp
template <typename T>
class Stack {
public:
    void push(const T& value);
    const T& top() const;
private:
    std::vector<T> data_;
};

// 类外定义:必须带 template 头,且类名要带 <T>
template <typename T>
void Stack<T>::push(const T& value) {
    data_.push_back(value);
}

template <typename T>
const T& Stack<T>::top() const {
    return data_.back();
}
```

两个常见坑。一是忘了写 `template <typename T>` 头,编译器不知道 `Stack<T>` 里的 `T` 从哪来。二是类名忘了 `<T>`,写成 `void Stack::push(...)`,这是把 `Stack` 当成具体类了,而 `Stack` 只是模板名,必须带 `<T>` 才表示某个实例化。

类外定义的好处是头文件清爽,坏处是模板的包含模型要求定义依然得在使用点可见,所以类外定义通常还是得写在头文件里(或者同一个头文件的末尾)。这跟普通类的「声明在 .h、实现在 .cpp」还是不一样。多数小型类模板直接把成员函数写在类内,省事;大型模板库(比如 STL 实现)会把成员函数拆到类外,并把它们放在同一个头文件或 `.tpp`(template implementation)文件里再被头文件 include。

## 依赖名 vs 非依赖名:`typename` 消歧义

这是类模板里最容易让人懵的一点。先记住两个名词:

- **非依赖名**(non-dependent name):不依赖任何模板参数的名字。比如 `int`、`std::cout`,编译器在模板定义点就知道它是什么。
- **依赖名**(dependent name):依赖某个模板参数的名字。比如 `T::value_type`,它的具体含义要等 `T` 确定了才知道。

麻烦出在依赖名上。当编译器解析模板定义时,它还不知道 `T` 是什么,自然也就不知道 `T::value_type` 究竟是个类型,还是个静态成员变量,还是个别的东西。C++ 的规则是:**默认不假定它是类型**。如果您想让编译器把它当类型用,就得用 `typename` 关键字明确告诉它。

```cpp
template <typename Container>
void print_first(const Container& c) {
    typename Container::value_type first = *c.begin();
    std::cout << "first = " << first << "\n";
}
```

`Container::value_type` 在这里要当类型用(声明一个变量),前面必须加 `typename`。加上之后,编译运行没问题:

```bash
$ g++ -Wall -Wextra -std=c++20 typename_dis.cpp -o typename_dis && ./typename_dis
first = 10
```

去掉 `typename`,GCC 直接拦下,报错信息把规则说得很清楚:

```text
typename_bad.cpp:3:5: error: need 'typename' before 'Container::value_type'
      because 'Container' is a dependent scope
    3 |     Container::value_type first = *c.begin();
      |     ^~~~~~~~~~~
```

「`Container` is a dependent scope」,所以编译器不敢假定 `value_type` 是类型,得您显式声明。

什么时候要加 `typename`?经验法则:当一个依赖限定符(带模板参数的 `::`)出现在需要类型的位置(声明变量、做类型转换、做返回类型、做模板类型参数),就要加 `typename`。C++20 之后(P0634)放宽了一处:在 type-only context(类型别名 `using`、函数返回类型、`new` 的类型、数据成员声明这几类位置)可以省略 `typename`,但局部变量声明这种位置依然要写。养成「依赖名当类型用就加 typename」的习惯最省心,不会出错。

类似的还有 `template` 关键字消歧义。如果依赖名后面跟 `<`,编译器不知道这是模板还是小于号比较,这时要写 `T::template Foo<int>()` 明确告诉它 `Foo` 是模板。这条用得少,但碰到时知道有这个工具。

## dependent base 和 `this->`

类模板做继承时,会撞上另一个相关的坑。看这段:

```cpp
template <typename T>
struct Base {
    void helper() {}
    int data = 7;
};

template <typename T>
struct Derived : Base<T> {
    void call() {
        helper();   // 报错!
    }
};
```

`Derived` 继承自 `Base<T>`,在 `Derived::call` 里调用基类的 `helper()`。直觉上基类明明有 `helper`,应该能直接调。可编译器报错:

```text
dep_base.cpp:10:9: error: there are no arguments to 'helper' that depend on
      a template parameter, so a declaration of 'helper' must be available
   10 |         helper();
      |         ^~~~~~
```

原因和 `typename` 那条是同一套逻辑。`Base<T>` 是个 dependent base,它的具体长相要等 `T` 确定才知道。编译器在解析 `Derived` 的定义时,不去 dependent base 里查找非依赖名(像 `helper` 这种不带 `T` 的名字),因为它怕 `T` 的某个特化里 `helper` 可能是个变量、或者根本不存在,查了反而可能查错。

解决办法是让名字「带上依赖」,最常用的就是 `this->`:

```cpp
template <typename T>
struct Derived : Base<T> {
    int fetch() { return this->data; }   // this-> 让查找进入 dependent base
    void call() { this->helper(); }
};

int main() {
    Derived<int> d;
    d.call();
    std::cout << "fetch() = " << d.fetch() << "\n";   // 访问到 Base<T>::data = 7
}
```

`this->` 告诉编译器「这是当前对象的成员,您到实例化的时候再去找」,问题解决。编译运行通过:

```bash
$ g++ -Wall -Wextra -std=c++20 dep_base_ok.cpp -o dep_base_ok && ./dep_base_ok
fetch() = 7
```

另一个等价写法是显式限定 `Base<T>::helper()`,但这个写法会关闭虚函数的动态分派,所以如果 `helper` 是虚函数,别用 `Base<T>::`,用 `this->`。一般场景两者都行,`this->` 更通用。

这条规则和下一篇要讲的「两阶段查找」是一回事的两面。两阶段查找要求编译器在模板定义阶段就对非依赖名做查找,而 dependent base 里的名字在定义阶段还看不到,所以编译器干脆不查。理解了这个动机,您就不会觉得 `this->` 是「多余的语法噪音」,它是为了让模板在两阶段查找的框架下行为可预测。

## 静态成员:每个类型一份

类模板可以有静态数据成员。和普通类的静态成员不同,类模板的静态成员**每个实例化类型各有一份独立实例**。

```cpp
template <typename T>
struct Counter {
    static int instances;   // 声明
};

// 定义(类外):要带 template 头
template <typename T>
int Counter<T>::instances = 0;

// Counter<int>::instances 和 Counter<double>::instances 是两个不同的变量
```

`Counter<int>::instances` 和 `Counter<double>::instances` 是两个完全独立的静态变量,互不影响。这一点常被用来给每种类型做计数、做类型专属的缓存。

这里要分清一个坑:类模板静态成员的类外定义(`template <typename T> int Counter<T>::instances = 0;` 这种)本身带 `template` 头,是弱符号(weak symbol),**可以**放在头文件里被多个翻译单元包含,链接器会自动合并,不冲突。真正「只能在一个翻译单元」的是**非模板**静态成员(普通类的 static 成员),它在头文件里被多 TU 包含才会重复定义。如果嫌类外定义麻烦,C++17 的 `inline` 静态成员可以直接在类内初始化:

```cpp
template <typename T>
struct Counter {
    static inline int instances = 0;   // C++17:inline 静态成员,类内初始化,免类外定义
};
```

这是现代写法,省去了类外定义的麻烦。

下一篇咱们进特化与偏特化。类模板能偏特化(这一点跟函数模板不一样),偏特化的「模式匹配」语义是模板里最有表达力的部分,`std::vector<bool>` 的特殊实现、type traits 的整套套路都建立在它上面。
