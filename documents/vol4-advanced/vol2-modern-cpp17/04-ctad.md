---
chapter: 12
cpp_standard:
- 17
description: CTAD 让编译器从构造函数参数反推类模板的参数,把 std::pair<int,double> p(1, 2.5) 这种啰嗦写法收成 std::pair p(1, 2.5)。讲清隐式推导指南、手写 deduction guide,以及圆括号与花括号、最窄可行类型这几个坑
difficulty: intermediate
order: 4
platform: host
prerequisites:
- 可变参数模板:参数包的展开
- 完美转发:forwarding references 与引用折叠
reading_time_minutes: 13
related:
- 类型安全的 any:综合项目
tags:
- host
- cpp-modern
- intermediate
- 模板
- 泛型
- 类型安全
title: CTAD:类模板参数推导
---
# CTAD:类模板参数推导

上一篇咱们讲了完美转发,核心是函数模板的参数类型怎么靠 `T&&` 和引用折叠自动适配。这一篇换个方向:类模板的参数能不能也交给编译器推。C++17 之前,您每次用类模板都得把参数写在尖括号里:`std::pair<int, double> p(1, 2.5)`、`std::lock_guard<std::mutex> lk(m)`、`std::vector<int> v{1,2,3}`。麻烦的地方在于,这些参数编译器从构造参数里完全推得出来——传进去是 `int` 和 `double`,模板参数就该是 `int` 和 `double`,何必再让笔者手写一遍。C++17 的 CTAD(Class Template Argument Deduction,类模板参数推导)就是来收掉这层重复的。

这一篇咱们把 CTAD 的来龙去脉讲清楚:它从哪推、什么时候隐式推导够用、什么时候得手写一条 deduction guide(推导指南),以及几个特别容易踩的陷阱。

## 少写一摞尖括号:CTAD 基本用法

直接看效果。下面这段把尖括号全部省掉,编译器照旧推得出正确类型:

```cpp
std::pair p(1, 2.5);                       // 推 pair<int, double>
std::pair p2(1, 2);                        // 推 pair<int, int>
std::tuple t(1, 2.5, "hi");                // 推 tuple<int, double, const char*>
std::vector v{1, 2, 3};                    // 推 vector<int>
std::mutex m;
std::lock_guard lk(m);                     // 推 lock_guard<std::mutex>
```

<OnlineCompilerDemo allow-run
  title="CTAD 基本用法:pair/tuple/vector/lock_guard 省掉尖括号"
  source-path="code/examples/vol4/vol2-modern-cpp17/basic_ctad.cpp"
  description="用 static_assert 验证推导出的类型与手写尖括号完全一致。"
/>

运行结果:

```text
p   = (1, 2.5)
t   = (1, 2.5, hi)
v   = {1, 2, 3}
所有 static_assert 通过,CTAD 推导结果与手写尖括号一致
```

`std::pair p(1, 2.5)` 推出 `pair<int, double>`,`std::pair p2(1, 2)` 推出 `pair<int, int>`,两个例子放一起就能看出 CTAD 的脾气:模板参数是怎么来的,完全跟着构造实参的类型走。`std::lock_guard` 这个例子更直观——它只有一种模板参数,就是互斥量的类型,传 `std::mutex m` 进去,推出来的自然是 `lock_guard<std::mutex>`,过去那串尖括号纯粹是冗余。

## 推导从哪来:隐式指南

CTAD 不是凭空变出来的,它的依据是「推导指南」(deduction guide)——一条告诉编译器「看到这种构造实参,就推出这种模板参数」的规则。您不写,编译器也会替每个构造函数隐式生成一条。咱们看个最小例子:

```cpp
template <typename T>
struct Box {
    T value;
    Box(T v) : value(v) {}
};

Box b(42);      // 隐式指南:Box(T) -> Box<T>,实参是 int,推出 Box<int>
```

`Box` 有一个构造函数 `Box(T v)`,编译器据此隐式生成一条推导指南 `Box(T) -> Box<T>`(箭头后面是推出的模板实例)。您写 `Box b(42)`,编译器拿实参 `42`(类型 `int`)去匹配这条指南,`T = int`,就推出 `Box<int>`。整个过程等价于您手写 `Box<int> b(42)`。

::: warning 隐式指南只看构造函数的参数
隐式指南的「线索」全是构造函数参数的类型。如果某个模板参数在构造函数签名里根本没出现(比如非类型参数 `N`),隐式指南就推不出它。`std::array` 之所以需要手写指南,原因就在这里,下面马上讲。
:::

## 手写推导指南:为什么 `array` 的 `N` 能推出来

`std::array` 的模板参数有两个:元素类型 `T` 和大小 `N`。`std::array a{1, 2, 3}` 能推出 `std::array<int, 3>`,但这件事靠隐式指南做不到。原因是 `std::array` 是个聚合类型,它压根没有那种「把 N 写进参数列表」的构造函数,编译器从构造签名里看不到任何关于 `N` 的线索。咱们自己仿写一个,把这点看清楚:

```cpp
template <typename T, std::size_t N>
struct NoGuide {
    T data[N];
    NoGuide(T v) { for (std::size_t i = 0; i < N; ++i) data[i] = v; }
};

NoGuide ng(42);   // T=int 推得出来,N 是多少?推不出
```

`NoGuide(T v)` 这个构造函数里只出现了 `T`,`N` 在签名里完全缺席。编译器隐式生成的指南是 `NoGuide(T) -> NoGuide<T, N>`,但 `N` 没有来源,推导直接失败。GCC 16.1.1 报的核心是这一句:

```text
error: class template argument deduction failed:
error: no matching function for call to 'NoGuide(int)'
    template argument deduction/substitution failed:
      couldn't deduce template parameter 'N'
```

标准库的 `std::array` 怎么绕过去的?靠手写一条推导指南,把 N 从「元素的个数」里捞出来。咱们给自己的 `MyArray` 配一条:

```cpp
template <typename T, std::size_t N>
struct MyArray {
    T data[N];
    MyArray(const T (&arr)[N]) {
        for (std::size_t i = 0; i < N; ++i) data[i] = arr[i];
    }
};

// 手写推导指南:从 C 数组的引用反推元素类型和大小
template <typename U, std::size_t N>
MyArray(const U (&)[N]) -> MyArray<U, N>;

int raw[] = {1, 2, 3, 4};
MyArray ma(raw);   // 指南出手:MyArray<int, 4>
```

指南的语法是 `TemplateName(参数模式) -> Name<推出的参数>`。这条指南的参数模式是 `const U (&)[N]`——一个长度为 `N` 的 `U` 数组引用。传 `int raw[4]` 进去,编译器从数组类型里同时推出 `U = int` 和 `N = 4`,于是 `MyArray ma(raw)` 拿到 `MyArray<int, 4>`。`N` 这个过去藏起来的非类型参数,被这条指南从数组的尺寸里挖了出来。

<OnlineCompilerDemo allow-run
  title="std::array 的 CTAD + 仿写一条 array 风格推导指南"
  source-path="code/examples/vol4/vol2-modern-cpp17/array_ctad.cpp"
  description="标准库 std::array a{1,2,3} 推 array<int,3>;自己的 MyArray 配一条手写指南,从 C 数组反推 N。"
/>

运行结果:

```text
std::array a: size=3  [0]=1  [2]=3
MyArray ma: 1 2 3 4
```

标准库实际给 `std::array` 写的指南比这条更讲究(用的是变参包配 `common_type` 来处理花括号初始化列表,所以 `std::array a{1,2,3}` 这种写法才直接成立),但底层逻辑就是上面这条:既然 `N` 不能从构造参数推,那就找另一条能看见 `N` 的路径,写进指南。

## 自己写一条推导指南

手写指南最常见的用法,是给那些「模板参数里有一部分不该让用户操心」的类型一个默认推导。咱们写一个 `Scaled<T, Scale>`,构造时只传一个值,`Scale` 在指南里固定成 `1`:

```cpp
template <typename T, int Scale>
struct Scaled {
    T value;
    constexpr Scaled(T v) : value(v * Scale) {}
};

// 指南:只看见 T,Scale 固定成 1
template <typename T>
Scaled(T) -> Scaled<T, 1>;

constexpr Scaled s(42);    // Scaled<int, 1>,value = 42
Scaled d(2.5);             // Scaled<double, 1>
Scaled<int, 10> big(5);    // 显式指定 Scale,value = 50
```

`Scaled(T) -> Scaled<T, 1>` 这条指南告诉编译器:看到 `Scaled(某个 T 类型的值)`,就推 `Scaled<T, 1>`,把 `Scale` 固定成 `1`。如果您想换 `Scale`,绕过 CTAD 直接把尖括号写全就行,`Scaled<int, 10> big(5)` 不受指南约束。指南提供的是「最常用的默认路径」,不是唯一入口。

<OnlineCompilerDemo allow-run
  title="手写推导指南:固定一个模板参数,保留元素类型"
  source-path="code/examples/vol4/vol2-modern-cpp17/custom_deduction_guide.cpp"
  description="Scaled(T) -> Scaled<T,1> 把 Scale 钉成 1;Wrapper(T) -> Wrapper<T> 保留元素类型(含 const char*)。"
/>

运行结果:

```text
s.value=42
d.value=2.5
big.value=50
w.data=hello
```

最后那个 `Wrapper w("hello")` 推出 `Wrapper<const char*>`,是因为指南 `Wrapper(T) -> Wrapper<T>` 把 `T` 留给了编译器从实参推,字符串字面量的类型是 `const char*`,T 就落定成它。指南写法就这么两步:箭头左边写「编译器看到什么样的构造实参」,箭头右边写「据此推出什么模板实例」。

## 三个容易踩的陷阱

CTAD 用顺手了很快,但有几个坑值得提前知道。

**第一,圆括号和花括号语义不同。** 这是 CTAD 最坑人的地方,因为它跟构造函数重载决议直接挂钩。同样是 `std::vector`,写法不同结果完全不同:

```cpp
std::vector a(10, 0);     // (count, value):10 个 0
std::vector b{10, 0};     // {initializer_list}:两个元素 10 和 0
```

`a` 的 size 是 10,每个元素是 0;`b` 的 size 是 2,装的是 10 和 0 两个值。圆括号走的是 `(count, value)` 那条构造,花括号优先匹配 `initializer_list` 构造。CTAD 本身没变,变的是「编译器选了哪条构造路径」,顺带连推导结果也跟着不同。

**第二,initializer_list 推导取的是「公共类型」,不是首元素类型。** 这是笔者一开始猜错的地方,值得专门说。咱们看:

```cpp
std::vector same_int{1, 2, 3};    // 全 int -> vector<int>
std::vector mix{1, 2.5};          // int+double -> vector<double>
```

`mix` 推出的是 `vector<double>`,不是 `vector<int>`。原因是 `std::vector` 那条 `initializer_list<T>` 指南要求所有元素都能塞进同一个 `T`,`int` 和 `double` 的公共类型是 `double`(`int` 升到 `double` 不窄化,反过来会窄化),所以 `T = double`。这条「取公共类型、不窄化」的规则,解释了为什么 `std::pair p(1, 2.5)` 推 `pair<int, double>` 而不是塞成一个类型——pair 的构造函数每个参数各自独立推,不需要公共类型;而 vector 的 `initializer_list<T>` 只有一个 `T`,必须取公共值。

如果您硬塞一对没法非窄化收敛的类型,推导就会失败。比如:

```cpp
std::vector bad{1, 2, 3, 100000000000LL};   // int 与 long long 没有非窄化公共类型
```

GCC 16.1.1 报:

```text
error: class template argument deduction failed:
error: no matching function for call to 'vector(int, int, int, long long int)'
```

`std::array a{1, 2.5}` 也是同样的毛病,它甚至比 vector 更严格——array 的指南要求所有元素类型完全一致,`int` 和 `double` 直接推不出唯一的 `T`。

**第三,复制构造推导保持元素类型。** 您拿一个现成的容器去初始化另一个,推导结果跟着源容器走:

```cpp
std::vector<int> src{1, 2, 3};
std::vector cpy(src);    // 复制构造推导,推 vector<int>
```

这里走的是复制构造那条隐式指南,源是 `vector<int>`,目标也推成 `vector<int>`。它和上一条花括号规则的区别在于:复制构造的实参本身已经是个 `vector<int>`,它的元素类型是确定的,不存在「取公共类型」这步。

<OnlineCompilerDemo allow-run
  title="圆括号 vs 花括号、复制推导 vs initializer_list、公共类型不窄化"
  source-path="code/examples/vol4/vol2-modern-cpp17/deduction_traps.cpp"
  description="默认演示三个陷阱的对照案例;加 -DNARROW_FAIL 复现 vector{int..., long long} 推不出公共类型的报错。"
/>

运行结果:

```text
a (10,0): size=10  [0]=0  [9]=0
b {10,0}: size=2  [0]=10  [1]=0
cpy: size=3  [2]=3
mix {1, 2.5}: [0]=1  [1]=2.5  (类型是 vector<double>)
```

## 几个边界,顺带提一下

CTAD 还有两个边界,知道有这回事就行,不必深挖。

一个是「非推导上下文」。某些位置编译器天然不拿去反推模板参数,最典型的是 `TypeName::value_type` 这种内嵌类型。您写一条 `Wrap(typename Wrap<X>::value_type) -> Wrap<X>` 这样的指南,编译器不会从实参的 `value_type` 倒推 `X`。好在绝大多数类模板的隐式指南就够用了,需要绕非推导上下文的场景很少。

另一个是 `explicit` 构造。`explicit` 影响的是「隐式转换」这条路径(比如函数形参是 `ExplicitSingle<int>`,您直接传个 `42` 会被挡),对 CTAD 的直接构造没有影响。`ExplicitSingle es(42)` 照样能推 `ExplicitSingle<int>`,`explicit` 只管不让 `42` 偷偷变成 `ExplicitSingle<int>`。

下一篇咱们把视线从「类模板」挪到「类型擦除」——讲讲怎么用 `std::any` 装下任意类型的值,同时尽量保住类型安全。CTAD 在那里会再露一次脸,帮咱们省掉一些尖括号。
