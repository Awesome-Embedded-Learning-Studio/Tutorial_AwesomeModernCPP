---
chapter: 0
cpp_standard:
- 11
- 14
- 17
description: 理解 C++ 值类别体系，掌握右值引用的绑定规则与核心语义
difficulty: intermediate
order: 1
platform: host
prerequisites:
- 卷一：C++ 基础入门
reading_time_minutes: 24
related:
- 移动构造与移动赋值
- 完美转发
tags:
- host
- cpp-modern
- intermediate
- 移动语义
title: 右值引用：从拷贝到移动
---
# 右值引用：从拷贝到移动

欢迎来到现代 C++！咱们这套教程里说的"现代 C++"，一般指 C++11 以及之后的版本。

> 有朋友会来争议的，笔者自己交流的时候就被喷过「C++11 还算现代？」嗯……好像也没问题，从笔者落笔的 2026 年算起，它们已经陪咱们走了十几年，时间上其实不算现代。不过和 C++98 的老古董们相比，它已经有了相当大的特性变更。这就是这一卷被单独拿出来的原因！

笔者最开始接触 C++ 的时候啃《Effective Modern C++》<RefLink :id="1" preview="Scott Meyers, Effective Modern C++, 2014 — Items 23-25: rvalue references, universal references, std::move" />，总觉得「右值引用」的概念理解不透。您猜怎么着，右值引用啊，光这名字就散发着一种说不清的学术味——`T&&` 究竟是什么呢，左值和右值的界线又在哪里，`std::move` 是不是真的在"移动"什么东西？每次看到别人的代码里出现 `std::move`，笔者总是似懂非懂地抄过来，剩下的全指望编译器大发慈悲了。现在轮到笔者给您写教程了，那就得把这些内容陪您一起搞明白，至少不给您丢人，不犯很低级的错误！

> 依旧碎碎念：笔者挺害怕 C++ 语言律师的，每次提笔写东西的时候，都怕被这批大佬挑出自己的毛病。不过严谨从来是好的——写 C++ 不严谨的话，轻则咱们对着编译器吐的一屏报错发呆，重则您半夜被内存错误摇起来。教学呢，其实是另一回事，没必要一上来就跟您死抠细节，咱们可别一叶障目。

## 从一个让人血压升高的问题说起

咱们现在设想的场景是字符串处理，大家都知道对吧。不少人会觉得——欸，std::string 有的时候太重了，想弄一个字符串的只读视图。const char* 看着是挺合适的，可 C 字符串得靠结尾的 \0 收尾，这件事有时候就很不靠谱：内容里存不了 \0，长度也只能靠咱们从头现数一遍。那咱们就自己动手做一个 StringWrapper！

```cpp
class StringWrapper {
    char* data_;
    std::size_t size_;

public:
    StringWrapper(const char* str)
    {
        size_ = std::strlen(str);
        data_ = new char[size_ + 1];
        std::memcpy(data_, str, size_ + 1);
    }

    // 拷贝构造：深拷贝
    StringWrapper(const StringWrapper& other)
        : size_(other.size_)
    {
        data_ = new char[size_ + 1];
        std::memcpy(data_, other.data_, size_ + 1);
    }

    ~StringWrapper()
    {
        delete[] data_;
    }
};
```

然后咱们写一段看起来很无辜的代码：

```cpp
StringWrapper build_greeting(const std::string& name)
{
    StringWrapper result(("Hello, " + name + "!").c_str());
    return result;
}

int main()
{
    StringWrapper greeting = build_greeting("World");
    return 0;
}
```

咱们假设一个最坏情况：没有移动语义，您再假设编译器也没做 NRVO（命名返回值优化）。您按这个假设想下去，`build_greeting` 在返回 `result` 的时候，就得触发一次拷贝构造：咱们得分配一块新内存，把 `result` 里的字符串逐字节复制过去。您再看 `result` 自己，析构的时候把原来那块内存也释放掉了。（您想想，这样的操作是不是亏得发麻？）

> 当然，咱们得把话补全。C++03 时代的 GCC 和 MSVC，那时候就已经普遍支持 NRVO 了。标准向来是允许编译器省掉这次拷贝的，只是不强制而已，所以上面这段分析讨论的，是"NRVO 不生效"时的最坏情况。

咱们花了一次内存分配加一次逐字节拷贝，只为了把一个马上就要销毁的对象里的数据"搬到"另一个位置上。如果字符串很长的话，比如一个几 KB 的 JSON 文本，这样的拷贝就显得格外浪费：**源对象反正马上就要死了，数据留在那块内存里也是白搭的，为什么不直接把内存的控制权接管过来？**

移动语义要解决的，正是咱们刚才看到的浪费。而要看懂移动语义，咱们就得回到一个更基础的问题上：C++ 是怎么给表达式分类的？标准给出的答案，咱们把它叫作**值类别**（value category）<RefLink :id="2" preview="cppreference Value categories — lvalue / xvalue / prvalue taxonomy since C++11" />。

## 值类别：从二分到三分

C++11 之前咱们给表达式分类只用两个格子：**左值（lvalue）与右值（rvalue）**，就这么两分嘛。C++11 把"对象的资源可以安全转移"这件事引进语言之后，光有左右之分是不够用的，而分类也跟着变复杂了。咱们现在看到的体系是这样的：每个表达式，都恰好属于三者中的一类。至于三个类别的划分，**lvalue** 是有身份的一类，**xvalue** 是快过期的一类，**prvalue** 是纯临时的一类。往上归并之后您会得到两个更宽的类别。管左值那一半的，咱们叫它 **glvalue**（generalized lvalue），它收编的是 lvalue 加 xvalue。管右值那一半的，咱们叫它 **rvalue**，它收编的是 xvalue 加 prvalue。

如果您觉得这个分类体系有点绕，您别急，笔者一开始也跟着绕了半天呢。咱们可以从两个属性来理解它。头一个属性咱们叫它**有身份**（has identity），说的就是表达式有名字、能取地址。另一个属性咱们把它叫**可移动**（can be moved from），说的是表达式是临时的、资源可以被咱们安全地"偷走"。您把两个属性一交叉，三类表达式就各归各位了嘛：

|              | 不可移动 | 可移动                   |
| ------------ | -------- | ------------------------ |
| **有身份**   | lvalue   | xvalue（expiring value） |
| **没有身份** | ——       | prvalue（pure rvalue）   |

咱们举个普通的例子，变量 `int x = 10;` 里的 `x` 有名字、有地址、生命周期也还没结束，就该落在"有身份、不可移动"的格子里，算 lvalue——它还会被后面的代码使用，您当然不能随便把它的资源偷走。`std::move(x)` 的结果，仍然指向的还是 `x` 这个对象，只是被标记成了"即将过期、可以搬走资源"的样子，落在"有身份、可移动"的交叉点上成了 xvalue。字面量 `42` 或者函数按值返回的临时对象呢，本来就是没有名字的，落进 prvalue 的格子里，您不用担心搬了之后还有谁会访问它。至于"没有身份且不可移动"的组合嘛，标准压根没给这样的表达式留位置，反正咱们靠三类就把所有表达式分完了。

咱们再看一组具体的例子，把这三类的界线画清楚。

```cpp
int x = 10;            // x 是 lvalue
int&& r = std::move(x); // std::move(x) 是 xvalue
int y = x + 1;         // x + 1 是 prvalue
int z = 42;            // 42 是 prvalue
```

这里面 `x` 是最典型的 lvalue——有名字也有地址，`&x` 是合法的表达式（您当然可以在栈上拿到这个变量的地址！）。`std::move(x)` 产生的是一个 xvalue，它和 `x` 指向的还是同一块内存，只是语义上被标记成了"即将过期"的形态。`x + 1` 和 `42` 都是 prvalue——临时的、没有名字的值。

咱们还得拆掉一个经典的误区，就是那句流传很久的老话："左值可以出现在赋值号的左边，右值只能待在赋值号的右边"。这样的说法在 C 语言时代基本是成立的，可是到了 C++ 的时代，它其实既不充分、也不必要。咱们来看两个反例。头一个反例咱们就拿 `const int cx = 10;` 来说吧，这里的 `cx` 是 lvalue，但您写 `cx = 20;` 就编译不过了——const 限制的只是修改，并不改变它的值类别。反过来呢，咱们再构造一个 `std::string("hello")`，它是个临时的 prvalue，却真的能写在赋值号左边：`std::string("hello") = "world";` 这样写是完全合法的。其实原因不复杂：类类型的赋值运算符本来就是成员函数，咱们那行代码，本质是在一个临时对象身上调用了成员函数，而这件事从 C++98 起就成立。真正写不进赋值号左边的，是内置类型——您写 `42 = x;`，在任何标准下都是过不了编译的。

### 用 decltype 判断任意表达式的值类别

规则咱们基本是懂的，可咱们拿到一个陌生表达式，到底要怎么确认它的身份，是 lvalue、xvalue 还是 prvalue 呢？咱们总不能每次都靠脑补。这里有一个现成的技巧。`decltype` 对**标识符**和**加括号的表达式**，求出来的类型是不一样的。

咱们把 `x` 本身和 `(x)` 摆在一起看：`decltype(x)` 是不带括号的标识符，得到的是 `x` 的**声明类型**。而 `decltype((x))`，您把括号一带上，求类型的规则就换成了按值类别来判。lvalue 求出来的是 `T&`，xvalue 求出来的是 `T&&`，prvalue 求出来的是 `T` 本身。

咱们把这个差异套进 `is_lvalue_reference_v` / `is_rvalue_reference_v`，就能给任意表达式判断出它的值类别：

```cpp
// value_category_probe.cpp -- 用 decltype 判断任意表达式的值类别
// Standard: C++17

#include <iostream>
#include <type_traits>
#include <utility>

template <class T>
constexpr const char* value_category()
{
    if constexpr (std::is_lvalue_reference_v<T>) {
        return "lvalue";
    } else if constexpr (std::is_rvalue_reference_v<T>) {
        return "xvalue";
    } else {
        return "prvalue";
    }
}

// decltype((expr)) 按表达式的值类别求类型：lvalue 得 T&，xvalue 得 T&&，prvalue 得 T
#define SHOW(expr) \
    std::cout << "  " #expr "  ->  " << value_category<decltype((expr))>() << "\n"

int g = 100;  // 全局变量

int main()
{
    int x = 10;        // 普通变量
    int& lref = x;     // 左值引用
    int&& rref = 20;   // 右值引用（但 rref 这个名字本身是左值！）

    std::cout << "--- 变量与引用 ---\n";
    SHOW(x);
    SHOW(lref);
    SHOW(rref);          // 反直觉点：命名的右值引用是左值

    std::cout << "\n--- 字面量与运算 ---\n";
    SHOW(42);
    SHOW(x + 1);
    SHOW(std::move(x));  // std::move 的产物是 xvalue

    std::cout << "\n--- 解引用与成员 ---\n";
    SHOW(*(&x));         // 解引用得到左值
    SHOW(g);

    return 0;
}
```

探测器的完整代码就在下面，您点「动手试一试」就能直接跑，连终端都替您省了：

<OnlineCompilerDemo
  title="动手验证：value_category_probe.cpp"
  source-path="code/examples/vol2/14_value_category_probe.cpp"
  description="在线判定一批表达式的值类别。rref 那一行最值得咱们打起精神：声明成 int&& 的变量，名字本身却是左值。"
  run-options="-O0 -std=c++17"
  allow-run
/>

您把输出的每一行都过一遍，其中最值得弄明白的，是 `rref` 那一行——它明明声明成了的是右值引用 `int&& rref`，判定出来的结果却是 `lvalue`。这不是 bug：`rref` 是一个**有名字**的变量，而 C++ 的规则是"有名字的表达式就是左值"。`std::move(x)` 产出的那个 xvalue，一旦您给它起了名字（赋给一个 `T&&` 变量、或当作函数参数传进去），它就"降级"回到左值的那一边，连移动都不会再自动触发了。完美转发就是来解决这个的，咱们第四篇会专门拆。

有了这样一个现成的办法，您碰上任何拿不准的表达式，都能让代码替咱们确认，再也不用拍脑袋瞎猜了。

## 右值引用的绑定规则

您理解了值类别，咱们来看看右值引用——`T&&`——到底能绑定到什么上面。规则其实很简单：**右值引用只绑 prvalue 或 xvalue 一类的右值，而不绑左值**。

```cpp
int x = 10;

int&& r1 = 42;           // OK：42 是 prvalue
int&& r2 = x + 1;        // OK：x + 1 是 prvalue
int&& r3 = std::move(x); // OK：std::move(x) 是 xvalue

// int&& r4 = x;         // 编译错误：x 是 lvalue，不能绑定到右值引用
```

如果您取消注释最后一行，GCC 会给您一个相当直接的错误信息：

```text
error: cannot bind rvalue reference of type 'int&&' to lvalue of type 'int'
```

值类别的分法和右值引用的绑定规则做成了动画，您可以播放、暂停，也可以按步进键一格格地看：

<Anim id="lvalue-rvalue" />

规则为什么这么设计呢，咱们不难猜到它的意图：右值引用的用意，就是让您能够"接管"临时对象的资源。您要问，为什么不让它连左值一起绑呢，咱们比完 const 左值引用再回头细说。

这里笔者得给您提个醒哦："`T&&` 只绑右值"的规则，说的是**写死类型**的右值引用，比如 `int&&` 跟 `std::string&&` 这样的。等到第四篇咱们讲完美转发的时候，会碰到模板里的 `T&&`，咱们把它叫**转发引用**（forwarding reference），左值右值都能绑的，走的可是另一套规则。您要是拿这一篇的规则去套模板里的 `T&&`，编译器会直接把错误顶给您看。

现在咱们把右值引用和 const 左值引用放在一起比一比，这一比啊，直接决定了后面移动构造函数的签名会是怎么样的。

const 左值引用 `const T&` 呢，是什么都能绑的：左值、右值、const、非 const 一概来者不拒。而右值引用 `T&&`，就只绑右值而已。这样的差异，看起来是简单的，但它会引出一个非常要紧的实战区别：用 `const T&` 接收右值的时候，您承诺了不修改它，也就没法偷走它的资源。而改用 `T&&` 接收右值，您就有修改它的权限了，也才能安全地把资源转移走。

```cpp
void process_const_ref(const std::string& s)
{
    // 可以读取 s，但不能修改它
    // 所以无法"偷走" s 的内部缓冲区
    std::cout << s.size() << "\n";
}

void process_rvalue_ref(std::string&& s)
{
    // s 是非 const 的右值引用，可以修改它
    // 所以可以安全地转移 s 的内部资源
    std::string stolen = std::move(s);
    // 此时 s 处于"有效但未指定"的状态
}
```

现在回头解决刚才搁下的问题：为什么不让右值引用也绑定左值？咱们从移动语义的意图倒推一遍就明白了。移动语义表达的是资源所有权的转移，而一个左值是有自己地址、正掌管着自己资源的对象，反正谁也没说过它打算被搬空的。假如 `T&&` 什么都能绑的话，您就再也没法区分"这个对象可以安全搬"和"这个对象还在被人用"。这样的区分一旦没了，移动语义也就无从谈起了。

## std::move 的本质：一次类型转换

`std::move` 的名字，笔者愿称之为 C++ 历史上最具误导性的命名之一。您听这名字，八成以为它真的在"移动"什么东西，可实际上它可是什么都没移动的。`std::move` 只做一件事：**把它的参数转换成右值引用**，也就是个 `static_cast<T&&>` 而已<RefLink :id="3" preview="cppreference std::move — equivalent to static_cast to rvalue reference; moves nothing" />。真的，仅此而已。

咱们可以自己实现一个等价的 `move`：

```cpp
template<typename T>
constexpr typename std::remove_reference<T>::type&&
my_move(T&& t) noexcept
{
    return static_cast<typename std::remove_reference<T>::type&&>(t);
}
```

这段代码做的事情非常直接：不管传入的 `T` 是什么类型，咱们都用 `remove_reference` 把可能存在的引用去掉，然后咱们用 `static_cast` 把它转成右值引用。函数体到这里就完了，咱们这几行，干的正是标准库 `std::move` 的活。

那它的用处在哪里？用处得落到**移动构造函数和移动赋值运算符的签名**上。您写 `std::string a = std::move(b);` 的时候，是 `std::move(b)` 把 `b` 转换成了 `std::string&&`，匹配的落点就是移动构造函数。它长什么样呢？它的签名就是 `std::string(std::string&& other)`。真正执行"资源转移"的正是移动构造函数：它把 `other` 的内部缓冲区指针接过来，再把 `other` 置成空——典型实现就是这么干的<RefLink :id="4" preview="cppreference Move constructor — transfer resources, leave source valid but unspecified" />。`std::move` 在这里面出的力，就是把 `b` 标记成"可以被搬走"的右值，好让编译器替咱们在拷贝、移动两个构造函数之间挑中后面那个。

> 短字符串是唯一的例外：它们直接存在对象内部的小缓冲区里（SSO、Short String Optimization、短字符串优化），没有独立的堆缓冲可以让咱们接管，移动就退化成一次普通的拷贝。

```cpp
std::string a = "Hello";
std::string b = std::move(a);  // std::move 只是转换类型
                                  // 移动构造函数做了实际的资源转移
// 此刻 a 处于"有效但未指定"的状态
// 在大多数实现中 a 变成空字符串，但您不应该依赖这个行为
```

这里还有一个值得您警惕的误区：**对基本类型使用 `std::move`，是带不来任何性能收益的**。咱们再看看 `std::move(42)`，它只是把 `int` 转换成 `int&&` 而已，可 `int` 的"移动"和"拷贝"本来就是同一回事——都是复制四个字节，这是定义层面的事实，跟编译器优化不优化是没有关系的。移动语义的威力只体现在**管理了资源的类**上，比如持有动态内存、文件句柄、网络连接的类。

## 临时对象的生命周期——右值引用延长了什么

咱们在 C++ 里看，临时对象（prvalue）的默认寿命，只到包含它的完整表达式为止：表达式一结束它就析构。但右值引用和 const 左值引用给了咱们一个特殊待遇：当它们绑定到临时对象的时候，就会把这个临时对象的生命周期延长，让它活到引用的作用域结束<RefLink :id="5" preview="cppreference Reference initialization — temporary lifetime extension applies only to direct binding" />。

```cpp
const int& cr = 42;       // const 引用延长了 42 的生命周期
std::cout << cr << "\n";   // OK：42 还活着

int&& rr = 100;            // 右值引用也延长了 100 的生命周期
std::cout << rr << "\n";   // OK：100 还活着
```

这两者在延长生命周期上的行为是一样的，区别在于 `rr` 是非 const 的——您可以修改它。您看着可能有点怪，一个字面量 `100` 呢，怎么能被咱们修改？其实编译器在幕后把这个临时值放到了一块存储空间里，`rr` 指向的就是这块空间。

```cpp
int&& rr = 100;
rr = 200;                  // 合法！rr 指向的存储空间被修改了
std::cout << rr << "\n";   // 输出 200
```

这样的特性，实战里其实用得不多，但理解它能帮您消除对"右值引用是不是马上就悬空了"的恐惧。您写 `std::string&& ref` 去接 `std::move(name)` 的时候，`ref` 指向的对象不会在下一行就消失——它一直活到 `ref` 的作用域结束。

不过，咱们在这里要多问一句：const 左值引用和右值引用都能把临时对象的寿命延长到引用自己的作用域结束，那要是这个绑定经过了函数中转，待遇还在不在呢？咱们让代码来回答。最容易栽跟头的就是下面这样：

```cpp
// lifetime_dangle.cpp -- 临时对象生命周期延长的边界
// Standard: C++17
// 编译（ASan 即 AddressSanitizer，内存错误检测工具；开它的 use-after-scope 检测才能抓到悬空读）：
//   g++ -std=c++17 -O0 -g -fsanitize=address -fsanitize-address-use-after-scope \
//       -o lifetime_dangle lifetime_dangle.cpp

#include <iostream>
#include <string>

// 把参数引用原样返回
const std::string& pass_through(const std::string& s)
{
    return s;
}

int main()
{
    std::cout << std::unitbuf;  // 实时冲刷，确保 ASan 中断前的输出可见

    // 情况 1：const& 直接绑定一个临时对象——生命周期被延长，安全
    const std::string& safe = std::string("I am a temp");
    std::cout << "1) 直接绑定临时对象: \"" << safe << "\"\n";

    // 情况 2：const& 绑的是"函数返回的引用"，而这个引用指向一个临时对象——不延长，悬空
    const std::string& dead = pass_through(std::string("I am passed"));
    std::cout << "2) 经函数返回的引用: \"" << dead << "\"\n";  // use-after-scope

    return 0;
}
```

情况 1 是没有问题的，`const&` 稳稳地接住了临时对象，它的寿命一直延续到了 `safe` 离开作用域。情况 2 可就出事了——临时对象 `"I am passed"` 是绑到函数参数 `s` 上的，按标准它只活到**所在完整表达式的结束**。而 `dead` 接的是 `pass_through` 返回的引用，这样"经函数中转"的绑定不触发延长。于是在表达式结束的瞬间，临时对象就析构了。咱们下一行去读 `dead`，读到的已经是一个析构过的对象，这一步的结果是没有定义的。

这样的错误，肉眼是很难看出来的——不开 ASan 时它可能"碰巧"还打印出旧内容，因为那块内存的内容还保持着原样，这正是悬空引用最阴险的地方。咱们开了 ASan 的 use-after-scope 检测再跑一遍，真相马上就现出原形了：

```text
1) 直接绑定临时对象: "I am a temp"
2) 经函数返回的引用: "=================================================================
==PID==ERROR: AddressSanitizer: stack-use-after-scope on address 0x... at pc 0x...
    #2 ... in main lifetime_dangle.cpp:26
SUMMARY: AddressSanitizer: stack-use-after-scope ... in std::__ostream_insert
```

（进程号 `PID`、地址 `0x...` 每次的运行结果都不同，该省略的笔者都省略了，错误类型、行号、SUMMARY 是固定的。）情况 1 是正常打印的，情况 2 在咱们尝试读 `dead` 的瞬间被 ASan 当场按住。现在证据也到手了，咱们可以正式回答刚才问出的那一句了：**生命周期的延长只对"直接绑定"生效，是不跨函数边界的**——临时对象一旦经过了函数中转，延长规则也就跟着失效了。

## 实战：字符串拼接里的拷贝与移动

咱们把前面学的东西放在一起，看一个真实的例子。假设咱们在构建日志消息：

```cpp
#include <iostream>
#include <string>
#include <vector>

std::string build_log_message(
    const std::string& level,
    const std::string& module,
    const std::string& detail)
{
    std::string msg = "[" + level + "] " + module + ": " + detail;
    return msg;
}

int main()
{
    std::string log = build_log_message("ERROR", "Network", "Connection timeout");
    std::cout << log << "\n";
    return 0;
}
```

这里面的 `"[" + level + "] " + module + ": " + detail`，是一串链式的 `+`。在 C++03 的世界里，咱们每做一次 `+`，都会得到一个新的临时字符串！每一环的代价都是新分配一块内存，再把左边累积的内容整段抄过去，越往后拼越贵——您想想这多浪费！

而 C++11 之后，标准库给 `operator+` 补了一批接受右值引用的重载（比如 `operator+(std::string&&, const std::string&)`）。有了它们呢，咱们就可以继续使用链条中间的临时对象了：把下一段直接追加进它的缓冲区再交出去，而不是每拼一环就新起一个字符串、把前面所有字符重抄一遍。这样咱们数下来，新起临时字符串的只有第一环，后面都是往它的缓冲区里追加，容量不够的时候才扩容。扩容的代价，会平摊到每一次的追加上面，整条链拷贝的字符总量只跟最终消息的长度成正比。

等咱们走到 C++17，保证消除（guaranteed copy elision）来了，把"返回 prvalue"做成了硬性的保证：`operator+` 吐出的结果直接在最终接收者的位置上构造，连接收时的那一次移动都省了。C++11 时代编译器一般也会顺手帮咱们省掉这一次移动，不过它向来只是惯例而已。而 C++17 把它写进了语言的条文里，咱们拿到的是保证。

更直接的收益来自函数返回：咱们接着看 `build_log_message`，它返回的是 `msg`，编译器手里有两种现成的优化手段。NRVO 可以把这次的拷贝直接消掉。咱们退一步说，在没有 NRVO 的场合，C++11 也会自动地把 `msg` 当作右值来处理（隐式移动），调用 `std::string` 的移动构造函数。这一步只转移内部的指针，也不复制字符的数据，您看，代价一下子就轻了。

咱们再来看一个容器元素转移的例子：

```cpp
std::vector<std::string> names;

std::string name = "Alice";
names.push_back(std::move(name));  // 移动：name 的内部数据转移到 vector 中
// name 现在处于有效但未指定的状态，不要再使用它

names.push_back("Bob");   // 先从 const char* 构造临时对象，再移动进 vector
```

咱们看头一个 `push_back`，它用上的是移动语义：`std::move(name)` 把 `name` 转成右值引用，vector 调用 `std::string` 的移动构造函数来构造新元素。它的代价，是转移一个指针加两个 `size_t` 而已，而不是复制整个字符串内容。而后一个 `push_back("Bob")`，看上去像是直接构造的，可它其实比 `push_back(std::move(name))` 多了一步。多的是哪一步、移动到底替咱们省了什么，咱们用带追踪的类跑一遍，让输出跟您交代。

咱们给下面这个 `TrackedString` 装上追踪：构造、拷贝、移动、析构的每一步，都会在屏幕上留下属于自己的一行：

```cpp
// push_back_vs_emplace.cpp -- push_back vs emplace_back 的构造/移动/析构追踪
// Standard: C++17

#include <iostream>
#include <string>
#include <utility>
#include <vector>

class TrackedString
{
    std::string data_;

public:
    explicit TrackedString(const char* s) : data_(s)
    {
        std::cout << "  [ctor from const char*] \"" << data_ << "\"\n";
    }

    TrackedString(const TrackedString& other) : data_(other.data_)
    {
        std::cout << "  [copy ctor] \"" << data_ << "\"\n";
    }

    TrackedString(TrackedString&& other) noexcept : data_(std::move(other.data_))
    {
        std::cout << "  [move ctor] \"" << data_ << "\"\n";
    }

    ~TrackedString()
    {
        std::cout << "  [dtor] \"" << data_ << "\"\n";
    }
};

int main()
{
    std::cout << "=== push_back(TrackedString(\"Bob\")) ===\n";
    {
        std::vector<TrackedString> v;
        v.push_back(TrackedString("Bob"));
        std::cout << "=== done ===\n";
    }

    std::cout << "\n=== emplace_back(\"Alice\") ===\n";
    {
        std::vector<TrackedString> v;
        v.emplace_back("Alice");
        std::cout << "=== done ===\n";
    }

    return 0;
}
```

追踪程序就在下面的演示里，您点「动手试一试」跑起来，咱们就来数输出的行数：

<OnlineCompilerDemo
  title="动手验证：push_back_vs_emplace.cpp"
  source-path="code/examples/vol2/15_push_back_vs_emplace.cpp"
  description="在线对比 push_back 与 emplace_back 的构造链路。数一数两段在 === done === 之前各打印了几行：push_back 三行，emplace_back 一行。"
  run-options="-O0 -std=c++17"
  allow-run
/>

咱们来看 `push_back(TrackedString("Bob"))` 这一段，在 `=== done ===` 之前给您数出三行：头一行构造的是临时对象，第二行把它移动进了 vector，第三行才是临时对象自己的析构。咱们数下来，构造链路实际走了两步。`=== done ===` 之后再出现的那行 `[dtor] "Bob"`，是 vector 离开 `{ }` 作用域时析构它持有的元素，不属于构造过程的一部分。

前面 `push_back("Bob")` 多出来的那一步，答案就藏在上面的三行输出里：`"Bob"` 头一步走的是 `const char*` 构造函数，隐式转换出一个临时的 `std::string`，再作为右值落进 `push_back(T&&)` 的重载，最后被移动进了 vector。多出来的，就是造临时对象的那一步。移动全程只发生了这么一次，您看 `[move ctor]` 恰好就一行，深拷贝是从头到尾都没有发生的。而 `emplace_back("Alice")` 在 `=== done ===` 之前只有一行 `ctor`：它直接在 vector 的存储空间里就地构造，连临时对象带移动一块儿全给省了。您要真想连临时对象都不造，咱们就换 `emplace_back`。

## 动手实验——rvalue_demo.cpp

刚在 `emplace_back` 的输出里，咱们一次只看一个动作。这回收个大的：写一个完整的程序，把这一篇讲过的东西放进同一个 `main` 里，完完整整地跑一遍。

```cpp
// rvalue_demo.cpp -- 右值引用与值类别演示
// Standard: C++17

#include <iostream>
#include <string>
#include <utility>

class Tracker
{
    std::string name_;

public:
    explicit Tracker(std::string name)
        : name_(std::move(name))
    {
        std::cout << "  [" << name_ << "] 构造\n";
    }

    Tracker(const Tracker& other)
        : name_(other.name_ + "_copy")
    {
        std::cout << "  [" << name_ << "] 拷贝构造\n";
    }

    Tracker(Tracker&& other) noexcept
        : name_(std::move(other.name_))
    {
        other.name_ = "(moved-from)";
        std::cout << "  [" << name_ << "] 移动构造\n";
    }

    ~Tracker()
    {
        std::cout << "  [" << name_ << "] 析构\n";
    }

    Tracker& operator=(const Tracker& other)
    {
        name_ = other.name_ + "_copy";
        std::cout << "  [" << name_ << "] 拷贝赋值\n";
        return *this;
    }

    Tracker& operator=(Tracker&& other) noexcept
    {
        name_ = std::move(other.name_);
        other.name_ = "(moved-from)";
        std::cout << "  [" << name_ << "] 移动赋值\n";
        return *this;
    }

    const std::string& name() const { return name_; }
};

/// @brief 返回临时对象（prvalue）
Tracker make_tracker(std::string name)
{
    return Tracker(std::move(name));
}

int main()
{
    std::cout << "=== 1. 基本构造 ===\n";
    Tracker a("A");
    std::cout << '\n';

    std::cout << "=== 2. 拷贝构造 ===\n";
    Tracker b = a;
    std::cout << "  a.name = " << a.name() << "\n";
    std::cout << "  b.name = " << b.name() << "\n\n";

    std::cout << "=== 3. 移动构造（显式 std::move）===\n";
    Tracker c = std::move(a);
    std::cout << "  a.name = " << a.name() << "\n";
    std::cout << "  c.name = " << c.name() << "\n\n";

    std::cout << "=== 4. 返回临时对象 ===\n";
    Tracker d = make_tracker("D");
    std::cout << "  d.name = " << d.name() << "\n\n";

    std::cout << "=== 5. 移动赋值 ===\n";
    d = std::move(b);
    std::cout << "  b.name = " << b.name() << "\n";
    std::cout << "  d.name = " << d.name() << "\n\n";

    std::cout << "=== 6. 程序结束，析构顺序 ===\n";
    return 0;
}
```

完整程序就挂在下面的演示里，您点「动手试一试」跑起来。这一次咱们一步不少，连程序收尾的析构都对着输出看：

<OnlineCompilerDemo
  title="动手实验：rvalue_demo.cpp"
  source-path="code/examples/vol2/01_rvalue_reference.cpp"
  description="在线运行并观察 Tracker 对象的构造、拷贝构造、移动构造和析构顺序。"
  run-options="-O0 -std=c++17"
  allow-run
/>

第 1 步咱们一句带过：`Tracker a("A");` 就是普通的构造。第 2 步的输出里，`Tracker b = a;` 触发的是拷贝构造——`a` 是左值，能匹配上的只有拷贝构造函数，`b` 的名字变成了 `"A_copy"`。第 3 步里呢，咱们靠 `std::move(a)` 把 `a` 转成右值引用，匹配移动构造函数——`c` 的名字变成了 `"A"`（从 `a` 那里偷来的），而 `a` 的名字变成了 `"(moved-from)"`。

第 4 步是最有意思的一步，咱们多看两眼。`make_tracker("D")` 在函数的内部构造了一个 `Tracker("D")`，然后才走返回的那一步。您注意输出里只有一次构造——没有拷贝也没有移动。这是因为 C++17 的**保证消除**：返回 prvalue 的时候，编译器直接在调用者的空间里构造对象，连那一步的移动都省了。这还只是返回 prvalue 的情形。返回命名局部变量的时候，编译器还能省到什么样的程度，就是第三篇 RVO 与 NRVO 要专门拆的事。

第 5 步轮到的是移动赋值。`d = std::move(b);` 把 `b` 的资源转移给 `d`——`d` 原来的名字 `"D"` 被覆盖成了 `"A_copy"`，`b` 变成了只剩 `"(moved-from)"` 的名字。咱们还会注意到，`d` 原来的资源（那块存着 `"D"` 的内存）被正确释放了，因为咱们写的移动赋值运算符，在覆盖之前得确保旧资源会被妥善地清理。

第 6 步咱们看程序收尾：这几个对象会在离开 `main` 的时候按构造的逆序析构——头一个析构的是 `d`，接着轮到的是 `c`、`b`、`a`，这是语言保证的顺序。您顺带留意 `a` 和 `b`：它们早被搬空了，名字挂的已经是 `"(moved-from)"`，析构却照样发生了。"有效但未指定"里的"有效"，落到这里就是连析构都完整地走完了，咱们不能因为它被搬过就当它不存在。

下一篇就把这些规则落到代码上——咱们亲手给管理资源的类写移动构造和移动赋值，把"接指针、置空源对象"的动作做扎实，再顺手补齐所谓的 Rule of Five（五法则：析构、拷贝构造、拷贝赋值、移动构造、移动赋值，咱们要么五个一个都不声明，要声明咱们就得把五个放在一起考虑）。

<ReferenceCard title="参考文献">
  <ReferenceItem
    :id="1"
    author="Scott Meyers"
    title="Effective Modern C++: 42 Specific Ways to Improve Your Use of C++11 and C++14"
    publisher="O'Reilly Media"
    :year="2014"
    chapter="Items 23-25: rvalue references, universal references, std::move"
  />
  <ReferenceItem
    :id="2"
    author="cppreference.com"
    title="Value Categories"
    url="https://en.cppreference.com/w/cpp/language/value_category"
  />
  <ReferenceItem
    :id="3"
    author="cppreference.com"
    title="std::move"
    chapter="Notes: moved-from objects stay valid but unspecified"
    url="https://en.cppreference.com/w/cpp/utility/move"
  />
  <ReferenceItem
    :id="4"
    author="cppreference.com"
    title="Move Constructor"
    url="https://en.cppreference.com/w/cpp/language/move_constructor"
  />
  <ReferenceItem
    :id="5"
    author="cppreference.com"
    title="Reference Initialization"
    chapter="Temporary lifetime extension"
    url="https://en.cppreference.com/w/cpp/language/reference_initialization"
  />
</ReferenceCard>
