---
chapter: 12
cpp_standard:
- 11
- 14
- 17
description: 综合项目手写一个类型安全的 mini any,把前三篇的 if constexpr、变参模板、完美转发焊在一起,讲透类型擦除、any_cast 的 type_info 比对、in_place 转发构造,以及小对象优化 SBO
difficulty: intermediate
order: 5
platform: host
prerequisites:
- 'if constexpr:编译期分支'
- '可变参数模板:参数包的展开'
- '完美转发:forwarding references 与引用折叠'
- 'CTAD:类模板参数推导'
reading_time_minutes: 18
related:
- 'if constexpr:编译期分支'
- '完美转发:forwarding references 与引用折叠'
tags:
- host
- cpp-modern
- intermediate
- 类型安全
- 模板
- 泛型
- RAII
title: '综合项目:类型安全的 any'
---
# 综合项目:类型安全的 any

走到这一篇,咱们手里已经有了三样工具:`if constexpr` 在编译期挑分支、变参模板展开任意个参数、完美转发把实参的值类别原样递下去。这三样单独看都还规矩,合到一起能干一件很出彩的事,自己造一个类型安全的 `any`,也就是标准库里那个能持有「任意类型值」的小容器。

这个综合项目不图大,图把类型擦除这件事讲透。`std::any` 看着神秘,其实底层就是前面那些工具的组合。咱们从最朴素的「我想存任意类型」开始,一步一步把 `void*` 踩过的坑、`type_info` 怎么救场、虚拷贝怎么让 any 可拷贝、in_place 怎么省掉多余的 move、SBO 怎么省掉堆分配,全都手写一遍。读完您再看标准库的 `<any>` 头,会发现它就是咱们这个 mini 版的工业级实现。

## 先看 `void*` 为什么不够用

需求很直接。一个变量,有时想存 `int`,有时想存 `std::string`,有时想存自定义结构体。第一反应多半是 `void*`:

```cpp
void* data_;
data_ = new int(42);
data_ = new std::string("hello");
```

能存进去,但取出来就麻烦了。`void*` 把类型信息整个扔了。要取回那个 `int`,得这么写:

```cpp
int value = *static_cast<int*>(data_);
```

这句转型完全靠人记得「当初存的是 int」。万一记错了呢?咱们把存 `int` 的 any 当 `double` 取出来看看会怎样。下面是一个只存 `void*`、啥都不记的「假 any」:

```cpp
class UnsafeAny {
    void* data_;
public:
    template <typename T>
    explicit UnsafeAny(T value) : data_(new T(value)) {}
    ~UnsafeAny() {}                      // 连析构都没法做对,这里干脆泄漏

    template <typename T>
    T get_as() const { return *static_cast<T*>(data_); }   // 想转什么就转什么
};
```

跑一下,存 `42` 取成 `double`:

<OnlineCompilerDemo allow-run
  title="假 any:void* 强转完全不校验类型,存 int 取 double 胡说八道"
  source-path="code/examples/vol4/vol2-modern-cpp17/any_cast_safety.cpp"
  description="UnsafeAny 只存 void*,get_as<double> 把 4 字节 int 当 8 字节 double 的位模式读,得到一个垃圾值,程序却一声不吭。"
/>

运行结果(节选):

```text
=== 假 any:存 int,取成 double,程序不报错但胡说 ===
  存 42,取成 double 得到: 2.07508e-322
```

`2.07508e-322`,一个近乎零的垃圾数(您自己跑出来的数字几乎肯定不一样,取决于那块内存里残留什么)。内存里那个 `42` 只占 4 字节,`double` 硬要按 8 字节的位模式去读,高位读到的全是未初始化的垃圾,结果完全错误,程序还不报错。这就是 `void*` 方案的根本问题:**类型信息丢了,转型成了信仰**。析构也是同一个坑,您不知道 `data_` 指的那个对象到底该调哪个析构函数,`delete` 都没法写。

要修好这两件事,any 得在存值的同时,把「这是什么类型」「怎么析构」「怎么拷贝」「怎么安全地转回去」这一组与类型相关的操作,跟值一起带上。这就是类型擦除要解决的问题。

## 类型擦除:用一个统一的外壳藏掉具体类型

类型擦除(type erasure)这个词听着玄,核心就一句话:**外层给一个统一的接口,内部用模板记住具体类型,把差异藏在外壳里**。

`std::any` 是这个模式的经典案例。外层的 `any` 类对所有类型长得都一样:就一个固定大小的对象,内部持一个「句柄」。句柄背后真正存值的那个对象,才知道自己到底装的是 `int` 还是 `string`。外层只通过一组与类型无关的操作跟句柄打交道:

- **类型问询**:`type()` 返回 `type_info`,告诉外面「我现在装的是什么类型」
- **析构**:句柄知道怎么析构自己持有的对象
- **克隆**:句柄能拷贝出一份新的自己
- **安全转型**:外面要用 `any_cast<T>` 取值,先比对 `type_info`,对得上才转

实现这套「外层统一、内部记忆具体类型」有两套主流写法。咱们选其中一条讲透,另一条点出差异。

## 路线 A:虚函数风格,基类指针 + 模板派生

这条路线最直观。外层 any 持一个指向「概念基类」的指针,基类把那一组操作定义成虚函数;一个模板派生类 `data_holder<T>` 才真正存值,并落实这些虚函数。实例化 `data_holder<int>` 时,编译器就生成了「专门装 int」的那个派生类。

```cpp
class Any {
private:
    struct concept_any_base {                              // 概念基类
        virtual ~concept_any_base() = default;
        virtual const std::type_info& type() const noexcept = 0;
        virtual std::unique_ptr<concept_any_base> clone() const = 0;
        virtual const void* untyped() const noexcept = 0;  // 交出内部指针
    };

    template <typename T>
    struct data_holder final : concept_any_base {          // 真正存 T 的派生类
        T data;
        // ... 落实上面那几个虚函数
    };

    std::unique_ptr<concept_any_base> holder_;             // 外层只看到基类指针
};
```

外层完全不知道 `data_holder<T>` 长什么样,它只握着一个 `concept_any_base*`。这就是类型擦除在起作用:`T` 这个信息被关在了派生类里,外层用一个统一的基类接口去操作它。

`data_holder<T>` 把那几个虚函数落实下来。`type()` 返回 `typeid(T)`,`clone()` 用 `make_unique` 重新造一个装着同样 `T` 的派生类,`untyped()` 把内部 `data` 的地址以 `const void*` 交出去:

```cpp
template <typename T>
struct data_holder final : concept_any_base {
    T data;
    template <typename... Args>
    explicit data_holder(Args&&... args) : data(std::forward<Args>(args)...) {}

    const std::type_info& type() const noexcept override { return typeid(T); }
    std::unique_ptr<concept_any_base> clone() const override {
        return std::make_unique<data_holder<T>>(data);
    }
    const void* untyped() const noexcept override { return &data; }
};
```

这里有个细节值得停一下。`data_holder` 的构造函数是个变参模板,配 `std::forward<Args>(args)...`(这是上一篇讲的完美转发)。它本身不挑食,任意个数、任意值类别的实参都能接,然后原样转发给 `T` 的构造。这一个构造函数,既支撑了「从现成的值构造 any」,也支撑了接下来要讲的 in_place 直接构造,一份代码两用。

::: warning 路线 B:函数指针表,标准库的真实做法
`std::any` 在主流实现里走的是另一条路,不用虚函数。它内部持一个 `void*` 加一组函数指针(`destroy`、`copy`、`move`、`cast`),这几个函数指针由模板函数按 `T` 生成,构造时存进表里。差别在于:虚函数风格靠虚表做分派,多一次虚调用;函数指针表风格把分派摊平成几次函数指针调用,布局更紧凑,SBO(小对象优化)更好做。功能上两者等价。咱们选路线 A 讲,因为它把「基类接口 + 模板派生」这个 OO 范式讲得最清楚,而这套范式也是 `std::function`、`std::shared_ptr` 的 deleter 这些类型擦除场景的通用骨架。
:::

## 构造任意类型,完美转发一份进去

外壳搭好了,接下来让 any 能装下任意值。最朴素的是从现成的值构造:

```cpp
template <typename T,
          typename DT = std::decay_t<T>,
          typename = std::enable_if_t<!std::is_same_v<DT, Any>>>
Any(T&& value)
    : holder_(std::make_unique<data_holder<DT>>(std::forward<T>(value))) {}
```

三处合起来看就清楚了。`T&&` 是 forwarding reference,实参是 lvalue 就推导成左值引用、是 rvalue 就推导成右值引用(上一篇的引用折叠)。`DT = decay_t<T>` 把推导出来的类型剥掉引用和顶层 `const`,得到「真正要存的类型」,比如传 `const int&` 进来,`DT` 就是 `int`。最后 `forward<T>(value)` 把值按原来的类别转发给 `data_holder` 的构造函数。

那个 `enable_if` 是为了挡住拷贝构造:`Any a = b;` 的时候 `T` 会推导成 `Any&`,要是没有这条约束,这个模板构造函数会比预定义的拷贝构造更匹配,编译就乱了。挡掉 `DT == Any` 的情况,把拷贝让回正常的拷贝构造函数去。

光有这个还不够理想。设想 held 类型很重,咱们手头只有它的构造参数,没有现成的对象。`Any a = Big{"x", 2};` 这种写法会先把 `Big` 临时对象构造出来,再 move 进 `data_holder`,白白多一次移动。标准库给了 in_place 构造来治这个:

```cpp
template <typename T, typename... Args>
explicit Any(std::in_place_type_t<T>, Args&&... args)
    : holder_(std::make_unique<data_holder<T>>(std::forward<Args>(args)...)) {}
```

`std::in_place_type_t<T>` 是个空的标签类型,作用是让这个构造函数的签名跟前面那个从值构造的重载区分开。`Args...` 是变参包,`std::forward<Args>(args)...` 把任意个构造参数原封不动地转发给 `T` 的构造函数。`T` 直接在 `data_holder` 内部「就地」长出来,省掉中间那次 move。

咱们数一数 move 次数,差别就看出来了。下面这个例子用一个带 move 计数的类型,对比两种构造方式:

<OnlineCompilerDemo allow-run
  title="in_place + 完美转发:held 对象就地构造,省掉多余的 move"
  source-path="code/examples/vol4/vol2-modern-cpp17/in_place_forward.cpp"
  description="方式 1 从现成的 Tracked 构造,临时对象要 move 进 holder;方式 2 用 in_place 把构造参数直接转发,Tracked 在 holder 里就地构造,move 次数为 0。"
/>

运行结果:

```text
=== 方式 1:先有 Tracked 临时对象,再 move 进 any ===
  [Tracked(string)] 直接构造, payload=hello
  [Tracked(&&)] 移动 #1
  方式 1 总移动次数 = 1, 总拷贝次数 = 0

=== 方式 2:in_place 把构造参数直接转发 ===
  [Tracked(string)] 直接构造, payload=hello
  方式 2 总移动次数 = 0, 总拷贝次数 = 0
```

方式 1 里 `Tracked{"hello"}` 先构造出一个临时对象,这个临时对象再被 move 进 `holder` 的 `data`,所以有一次移动。方式 2 把 `string("hello")` 这个构造参数直接转发给 `Tracked` 的构造函数,`Tracked` 在 `holder` 内部就地构造出来,全程零次额外的 move 或 copy。这就是 `std::any::emplace`、`std::make_unique`、`std::vector::emplace_back` 都用 in_place 的原因,省一次 move 对重型对象是实打实的收益。这里把变参模板的 `Args...` 和完美转发的 `forward<Args>(args)...` 拼在了一起,前三篇的工具在这里合流。

## any_cast:靠 `type_info` 比对做安全转型

存进去的值怎么安全地取回来,是 any 设计里最关键的一环。前面 `void*` 版的教训是转型完全没校验,存 `int` 取 `double` 直接胡说。`any_cast` 的做法是,转型之前先拿 `type_info` 比对,对不上就拒绝,绝不在没校验的情况下做 `reinterpret_cast`。

any 内部那个 `untyped()` 把 held 对象的地址以 `const void*` 交出来。`any_cast<T>` 拿到这个指针后,要先确认存的就是 `T`,再 `static_cast` 转回去。这一步的 `static_cast` 是安全的,因为 `untyped()` 返回的本来就是 `data_holder<T>::data` 的地址。咱们提供两个重载:指针版不匹配返回 `nullptr`,值版不匹配抛异常。

```cpp
// 指针重载:不匹配返回 nullptr,不抛异常
template <typename T>
const T* any_cast(const Any* a) noexcept {
    if (!a || !a->has_value() || a->type() != typeid(T)) {
        return nullptr;
    }
    return static_cast<const T*>(a->holder_->untyped());
}

// 值重载:不匹配抛 bad_cast(标准库用 bad_any_cast)
template <typename T>
T any_cast(const Any& a) {
    const T* p = any_cast<T>(&a);
    if (!p) throw std::bad_cast{};
    return *p;
}
```

跑一遍,看看安全转型在实际中怎么拦住错误:

<OnlineCompilerDemo allow-run
  title="完整的 mini any:存取 / 不匹配抛异常 / 指针版返回 nullptr / 深拷贝独立"
  source-path="code/examples/vol4/vol2-modern-cpp17/mini_any.cpp"
  description="存 int/string/Point 都能正确取回;存 int 取 double 被 type_info 比对拦下抛 bad_cast;指针版不匹配返回 nullptr;拷贝后两个 any 互不影响。"
/>

运行结果:

```text
=== 存取基础类型 ===
any_cast<int>(a)    = 42
any_cast<string>(b) = hello
any_cast<Point>(c)  = {1, 2}

=== 类型不匹配 -> 抛异常 ===
  bad_cast:存 int 取 double,被 type_info 比对拦下

=== 指针重载:不匹配返回 nullptr ===
  any_cast<int>(pa)  非空? 1
  any_cast<long>(pa) 非空? 0

=== 深拷贝:两个 any 独立 ===
  d 改成 string 后,a 仍是 int = 42

=== in_place 构造 ===
  e = "xxxx"
```

存 `int` 取 `int` 没问题,存 `int` 取 `double` 在转型之前就被 `type() != typeid(T)` 挡住,抛出 `bad_cast`。对比前面那个假 any 存 42 取出一个垃圾 double,同一个错误操作,真 any 一声断喝拦在转型前,假 any 静默地给您一个垃圾值。

### 一个容易踩的坑:`any_cast` 要精确匹配,不做隐式转换

`any_cast` 用 `typeid` 比对,`typeid` 有个特性:**顶层 `const` 被忽略,但类型本身得一模一样**。这意味着 `any_cast<const int>` 能取回存的 `int`,但 `any_cast<long>` 取存的 `int` 会失败,即使 `int` 能隐式转成 `long`。咱们在同一个例子里把这几条边界都验证了:

```text
typeid(int) == typeid(int):        1
typeid(int) == typeid(const int):  1  (顶层 const 被 typeid 忽略,可取)
typeid(int) == typeid(long):       0  (不同类型,拒)
typeid(int) == typeid(unsigned):   0  (不同类型,拒)
typeid(string) == typeid(const char*): 0  (完全不同,拒)
```

这条边界跟 `dynamic_cast` 那种多态转型是两码事。`dynamic_cast` 会沿继承链走,父类指针能转成子类指针;`any_cast` 不认继承,也不做数值类型的隐式转换,只认同一个 `type_info`。存 `int` 想取 `long`、存 `std::string` 想取 `const char*`,都得先显式转好类型再用 `any_cast` 取,否则一律失败。我专门拿标准库的 `std::any` 验过,行为跟咱们这个 mini 版完全一致:存 `int`,`any_cast<long>` 返回 `nullptr`,`any_cast<unsigned>` 也返回 `nullptr`,只有 `any_cast<int>`(以及顶层 const 的变体)能取回。

## 可拷贝的 any:靠虚 clone 落实

any 光能存取还不够,还得能拷贝。`Any d = a;` 这句要工作,得让 any 知道怎么复制自己持有的那个对象。问题是外层 any 不知道 held 类型是什么,没法直接拷贝。这里就轮到虚 `clone()` 出场。

基类把 `clone()` 声明成纯虚,派生类 `data_holder<T>` 落实它,内部就是 `make_unique<data_holder<T>>(data)`,也就是拷贝一份 held 对象再包进新的 `data_holder`。外层 any 的拷贝构造只要调一下虚 `clone()`:

```cpp
Any(const Any& other)
    : holder_(other.holder_ ? other.holder_->clone() : nullptr) {}
```

这一句背后发生的事,是 C++ 多态的典型场景。`other.holder_` 虽然是个基类指针,调 `clone()` 时会动态分派到正确的 `data_holder<T>::clone`,造出一个类型和值都一样的新句柄。前面那个深拷贝的运行结果里,`d` 拷贝自 `a`(装着 `42`),后来 `d` 被赋成 `string`,原对象 `a` 不受影响、仍是 `42`——两个 any 各持各的句柄,互不干扰。

::: warning 可拷贝性是 held 类型的要求
`clone()` 落实成 `make_unique<data_holder<T>>(data)`,这一步要求 `T` 可拷贝构造。如果存了一个只 move、不可拷贝的类型(比如 `std::unique_ptr`),实例化 `data_holder<unique_ptr>::clone` 的时候就会编译失败。any 的可拷贝性是「传染」的:整个 any 能不能拷贝,取决于它当前装的那个类型能不能拷贝。标准库的 `std::any` 在拷贝不可拷贝的 held 类型时会在运行期抛异常(因为它用了类型擦除的函数指针表,克隆函数注册成抛异常的桩),咱们的虚函数版本则在编译期就挡住。两种做法各有取舍,道理是一样的:拷贝语义得由 held 类型来保证。
:::

## 小对象优化:小类型就地存,大类型才上堆

目前的实现每次存值都要 `make_unique`,哪怕只存一个 `int` 也得去堆上分配一次。堆分配不便宜,标准库的 `std::any` 为了避开它,普遍做了**小对象优化(Small Buffer Optimization,SBO)**:留一块固定大小的内部 buffer,小类型直接 placement new 进 buffer,只有大类型才去堆上分配。

这个优化天然是 `if constexpr` 的用武之地。咱们给 any 加一块 buffer,构造时按 `sizeof(model<T>)` 在编译期挑路径:

```cpp
template <typename T, typename D = std::decay_t<T>,
          typename = std::enable_if_t<!std::is_same_v<D, SboAny>>>
SboAny(T&& v) {
    if constexpr (sizeof(model<D>) <= BUF) {
        ptr_ = new (buffer_) model<D>(std::forward<T>(v));   // 就地,无堆分配
        owns_heap_ = false;
    } else {
        ptr_ = new model<D>(std::forward<T>(v));             // 太大,堆上
        owns_heap_ = true;
    }
}
```

`if constexpr (sizeof(model<D>) <= BUF)` 这个条件在编译期就能算定,因为 `model<D>` 的大小在实例化时已知。小类型走第一支,`new (buffer_)` 是 placement new,在已有 buffer 上构造对象,不调用全局 `operator new`;大类型走第二支,正常堆分配。被丢弃的那一支压根不实例化,这正好是第一篇讲的 `if constexpr` 替代一摞偏特化的场景。

光构造挑路径还不够,拷贝也得跟着分派。咱们在基类里加两个虚函数:`clone_into(buf)` 试着塞进传入的 buffer,塞不下返回 `nullptr`;`clone_heap()` 在堆上克隆。派生类用 `if constexpr` 决定 `clone_into` 怎么走:

```cpp
concept_base* clone_into(char* buf) const override {
    if constexpr (sizeof(model<T>) <= BUF) {
        return new (buf) model<T>(data);   // 塞得下,就地
    }
    return nullptr;                         // 塞不下,告诉外层走堆
}
```

外层拷贝构造先试 `clone_into`,失败了再 `clone_heap`:

```cpp
SboAny(const SboAny& o) {
    if (!o.ptr_) return;
    ptr_ = o.ptr_->clone_into(buffer_);   // 先试塞自己的 buffer
    if (!ptr_) { ptr_ = o.ptr_->clone_heap(); owns_heap_ = true; }
}
```

咱们重载全局 `operator new` 来数堆分配次数,直观看看 SBO 的效果:

<OnlineCompilerDemo allow-run
  title="SBO:小类型零堆分配,大类型才上堆;if constexpr 在构造和 clone 时选路径"
  source-path="code/examples/vol4/vol2-modern-cpp17/sbo_any.cpp"
  description="重载 operator new 数分配次数:存 int(小)0 次,存 Big(128 字节)1 次;拷贝 int 也 0 次,拷贝 Big 1 次。if constexpr 按 sizeof(model<T>) 在编译期挑路径。"
/>

运行结果:

```text
BUF = 24 bytes (sizeof(void*)*3 on 64-bit)

=== 存 int:model<int> 很小,就地存 ===
  type==int? 1, on_heap? 0, 堆分配次数 = 0 (期望 0)

=== 存 Big:sizeof(model<Big>) > BUF,堆上存 ===
  type==Big? 1, on_heap? 1, 堆分配次数 = 1 (期望 1)

=== 拷贝 int:小类型拷贝走 clone_into,也不分配 ===
  b.on_heap? 0, 拷贝堆分配次数 = 0 (期望 0)

=== 拷贝 Big:大类型 clone_into 返回 nullptr,落回 clone_heap ===
  b.on_heap? 1, 拷贝堆分配次数 = 1 (期望 1)
```

`int` 全程零堆分配,`Big`(128 字节,超过 24 字节的 buffer)才会分配一次。拷贝行为也跟着分派:小类型拷贝走 `clone_into` 还是不分配,大类型拷贝因为 `clone_into` 返回 `nullptr`,落回 `clone_heap` 上堆。这就是 `if constexpr` 配合 `sizeof` 在编译期分派的典型用法,条件依赖模板参数 `T`,每个实例化结果都不一样,这就是它和普通 `if` 的差别。

::: warning SBO 改变了 sizeof,标准库不承诺 buffer 大小
加了 SBO 之后,`sizeof(SboAny)` 会变大(至少是 buffer 大小加上几个指针/标志)。这是用空间换堆分配的常见取舍,`std::function`、`std::string` 的短串优化都是同一套思路。标准库的 `std::any` 普遍做 SBO,但**不承诺 buffer 多大**,所以您不能假设 `sizeof(std::any)` 里能塞下多大的类型,这是实现定义的。咱们这个 mini 版把 buffer 定成 `sizeof(void*) * 3`(64 位下 24 字节)纯粹是为了演示。
:::

## 把工具合起来看

走到这里,这个 mini any 把前三篇的东西都用上了。变参模板 `template <typename... Args>` 让一个构造函数同时服务「从值构造」和「in_place 转发构造」两种用途;完美转发 `std::forward<Args>(args)...` 把构造参数原封不动地递给 held 对象的构造函数,省掉多余的 move;`if constexpr` 配 `sizeof` 在 SBO 里按 held 类型在编译期挑存储路径。类型擦除本身则靠「概念基类 + 模板派生」这套 OO 范式,把「存什么类型、怎么析构、怎么拷贝、怎么安全转型」这组与类型相关的操作,藏在一个统一的外壳背后。

`std::any` 的真实实现比这个 mini 版更精细:它用函数指针表替代虚函数以省掉虚表开销,把 SBO 的 buffer 大小和判定条件做了仔细的调优,`any_cast` 还要处理引用、指针各种重载。但骨架就是咱们写的这一套。您以后读到 `std::function` 持有任意可调用对象、`std::shared_ptr` 带自定义 deleter、`std::move_only_function` 持有只移动的回调,会发现它们用的都是同一个类型擦除骨架:外层统一接口,内部模板派生记住具体类型。这一篇是 vol2 子卷的收尾,把这些工具焊成一个能跑的小东西,也就把 Modern C++ 那套「在类型上做文章」的思路落到了实处。
