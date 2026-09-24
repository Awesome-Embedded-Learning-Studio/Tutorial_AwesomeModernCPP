---
chapter: 9
cpp_standard:
- 11
- 14
- 17
- 20
description: 从虚函数多态的边界出发看清重复代码的根源——类型写死在签名里，以及模板如何把类型变成编译期参数
difficulty: intermediate
order: 0
platform: host
prerequisites:
- OOP 实战
reading_time_minutes: 10
tags:
- cpp-modern
- host
- intermediate
- 进阶
title: 从继承到模板
---
# 从继承到模板：编译期的多态

上一章咱们给 `Canvas` 写的是 `vector<unique_ptr<Shape>>`：画布只认 `Shape*`，装进来的是圆是矩形它不关心，`draw()` 一调，虚函数自己分发到正确的版本。这套机制解决的是"多种类型共用一套接口"。现在换个需求，看着比画布简单得多：写一个栈，能装 `int`；再要一个装 `double` 的，再要一个装 `std::string` 的。push、pop、top 三件事，逻辑一个字都不差。咱们拿虚函数这套招数去试试，看它管不管用。

## int 进不了继承体系

虚函数路线有个从头到尾没明说的前提：参与进来的类型，必须出自同一个基类。`Circle` 能被 `Canvas` 统一操作，是因为它继承了 `Shape`，编译器才允许 `Shape*` 指向它。那 `int` 呢？咱们试着让它加入类层次：

```cpp
class Shape {
public:
    virtual ~Shape() = default;
};

class IntShape : public int {    // 让 int 加入类层次？
};
```

真跑一下，GCC 的报错是这样的：

```text
dead1.cpp:6:25: error: expected class-name before 'int'
    6 | class IntShape : public int {
      |                         ^~~
```

`int` 是内置类型，它根本不是类，谁也继承不了——咱们把 `int` 换成 `double` 再试，同样的报错再来一遍。继承这条路对内置类型整个关着。

硬走也有办法：给 `int` 造个壳，写一个 `class IntBox : public Box` 把值裹进去压栈，取出来再拆。能用，但代价咱们得看清楚：每个元素要在堆上单独构造一个对象，身上多背一个虚表指针，每次取值都走一遍虚函数。更难受的是类型有多少种，壳就得写多少个，`IntBox`、`DoubleBox`、`StringBox` 一路排下去。咱们本来想消灭重复代码，这一绕，重复反而更多了。这套壳方案有个出名的实例：Java 里 `List<int>` 不存在、只有 `List<Integer>`，等于把包壳定成了语言规则。

## 就算全是类，统一接口也写不出来

咱们把要求再降一档：只装自定义类，内置类型不要了。给所有元素一个共同基类，接口这么定：

```cpp
class StackBase {
public:
    virtual void push(const ???& value) = 0;    // ??? 处写什么？
};
```

`push` 的参数类型必须写死。写 `int`，`std::string` 就进不来；写个万能基类 `Object`，绕回上一节拆壳的老路。堵住的根源值得咱们停下来想清楚：继承解决的是"同一个类型家族里，运行时挑不同的实现"，而眼下这个问题正好反着——**要变的就是参数本身的类型**。类型这个东西，在继承体系里找不到位置。

## 把类型当参数传进去

问题的形状到这里咱们看清楚了：逻辑只有一份，变的只是类型。那就像函数传参一样，把类型也当成一个参数，写代码时先空着，用到时再填上。这就是模板（template）：

```cpp
template <typename T>
T smallest(T a, T b)
{
    return (a < b) ? a : b;
}
```

`template <typename T>` 声明这里有个类型参数 `T`，函数体里所有 `T` 都等实例化时替换成真实类型。语法的每个细节下一篇展开，咱们真跑一遍，看它是不是一份逻辑伺候了三种类型：

```cpp
std::cout << smallest(3, 7) << '\n';
std::cout << smallest(2.5, 1.5) << '\n';
std::cout << smallest(std::string("banana"), std::string("apple")) << '\n';
```

输出：

```text
3
1.5
apple
```

一次写完，`int`、`double`、`std::string` 三个版本全有了。继承、虚函数、包壳，这里一样都没用上。类也能这么干，`Stack<int>`、`Stack<std::string>` 写法下一篇讲，这里咱们先验证一件更要紧的事：模板生成的这些类型，彼此是什么关系？

```cpp
template <typename T>
class Stack {
public:
    void push(const T& v) { data_.push_back(v); }
private:
    std::vector<T> data_;
};

int main()
{
    Stack<int> a;
    Stack<double> b;
    Stack<int>* p = &b;    // 编译器会同意吗？
}
```

GCC 的答复：

```text
distinct.cpp:15:21: error: cannot convert ‘Stack<double>*’ to ‘Stack<int>*’ in initialization
   15 |     Stack<int>* p = &b;
      |                     ^~
```

不同意。编译器在告诉咱们：`Stack<int>` 和 `Stack<double>` 是两个互不相干的类型，跟 `int` 和 `double` 本身一个性质。它们都出自同一份 `Stack` 模板，但编译器照着它生成了两份独立的代码，这就是模板和继承最本质的区别：继承让一堆类型变成一家子，模板让一份代码变成一堆类型。所以在 `Stack<int>` 里，元素就是连续排布的裸 `int`，没有虚表指针，没有间接跳转，访问它和访问普通数组没有区别。

## 两条路各管各的事

到这里，咱们手里有了两套对付"多种类型"的办法：虚函数在运行时挑实现，模板在编译期生成实现。后者有个正式的名字，叫编译期多态（compile-time polymorphism），也叫静态多态。什么时候用哪条，判据其实很清楚：**具体类型要到运行时才知道**，用虚函数，`Canvas` 解析到什么图形画什么，写代码时确实没法预知；**类型在写代码时就定死了，纯粹是逻辑重复**，用模板，`Stack<int>` 在您敲下这行代码的那一刻就定了。

|  | 虚函数多态 | 模板泛型 |
|---|---|---|
| 挑选/生成实现的时机 | 运行时 | 编译期 |
| 类型之间的关系 | 必须同出一个基类 | 互不相干，撑得住这份逻辑就行 |
| 内置类型（`int`、`double`） | 进不来，只能包壳 | 直接支持 |
| 开销形态 | 每次调用一次间接跳转，还挡内联 | 零分派开销，但每种类型各生成一份代码 |
| 典型场景 | 插件、GUI 控件树、运行时解析输入 | 容器、通用算法、工具函数 |

两条路不是二选一，实际工程里它们经常配合。咱们回头看上一章的 `Canvas`：它持有 `vector<unique_ptr<Shape>>`，`vector` 本身是模板，管"容器装什么类型"；装进去的 `Shape*` 走虚函数，管"画布怎么画"。您可能还想起来 `emplace` 头上顶着的那行 `template <typename ConcreteShape, typename... Args>`——上一章咱们按下没表，现在可以说了，那就是模板，而且它已经在替咱们干活了。

接下来三篇咱们把模板的零件逐个拆开：函数模板把类型推导和几十行报错的读法讲清楚，类模板带咱们亲手实现一个泛型栈，特化则管"通用版本照顾不到的类型"该怎么单独安排。学完这三篇，`vector`、`string` 这些天天在用的东西是怎么造出来的，您就能看明白了——第 11 章的 STL，全靠这一章打底。

## 练习

### 练习 1：判断走哪条路

下面三个场景，请您各选虚函数或模板，并说一句理由：一个能装任意元素类型的顺序表；图形编辑器里可在运行时启用的插件式工具；解析 JSON 文件，值可能是字符串、数字或嵌套对象。

### 练习 2：找找上一篇的模板

请您回到上一篇 OOP 实战的代码，把模板出现过的位置都找出来（至少 4 处）。提示：`emplace` 头上那行只是最显眼的一个。

### 练习 3：讲给朋友听

请您不写代码，口头解释：为什么 `Shape*` 可以指向 `Circle`，而 `Stack<int>*` 不能指向 `Stack<double>`？能把这两种关系的区别讲明白，这一篇就算过关了。
