---
title: "Dense 层——span 视图与权重布局"
description: "造推理器第一个会算东西的层:Dense<In,Out> 用 std::span 视图指向外部权重存储而非自带值成员(实例从 392KB 压到 16 字节),forward 类内 inline 做 matvec,形状编译期锁死无需 std::expected。span 让 Stage 5 换 inline constexpr 权重时接口零改动。配套工程 code/volumn_codes/vol8-labs/ai/tiny_ml/stage2/"
chapter: 8
order: 15
platform: host
difficulty: advanced
cpp_standard: [23]
reading_time_minutes: 14
prerequisites:
  - "固定维度 Tensor——推理器的数据底座"
  - "权重为什么是 [Out, In]——行主序下的 cache 账"
related:
  - "Dense 在算什么——一次乘加,拆到每个输出"
  - "权重为什么是 [Out, In]——行主序下的 cache 账"
tags:
  - host
  - cpp-modern
  - advanced
  - 模板
  - 内存管理
  - 类型安全
---

# Dense 层——span 视图与权重布局

Stage 2 要造的是整个推理器第一个真正"会算东西"的层:一个 `Dense<In, Out>`,吃一个长度 In 的输入向量,吐一个长度 Out 的输出向量,算的是 [01 篇](./01-what-is-dense.md)拆过的 `y = W·x + b`。激活留给 Stage 3,这里只做仿射。配套工程在 `code/volumn_codes/vol8-labs/ai/tiny_ml/stage2/`。

这篇是 Stage 2 的实现主文档。如果你对 Dense 在算什么、权重为什么是 `[Out, In]` 还陌生,先读 [引入两篇](./index.md) 再回来;已经清楚的,这篇往下看就行。

## 为什么 Dense 第一个造

Stage 1 把数据底座 Tensor 造扎实之后,推理器就缺一个"让 Tensor 之间算起来"的部件。Dense 是咱们这个 MLP 里唯一的算术层(整个网络就两层 Dense 夹一个 ReLU),它一立起来,Stage 3 的 ReLU/Argmax 就只是对单个 Tensor 做逐元素或归约操作,Stage 4 的串联也就只是把几个层接成流水线。所以 Dense 是后面所有 stage 的算术基础,跟 Tensor 一样,它一塌后面全得跟着重写。

## 三个要先拍板的设计决策

### 决策一:权重不自带存储,只持 `std::span` 视图

这是 Stage 2 最承重的一个决策,也是跟"直觉写法"分歧最大的地方。

直觉写法是把权重和偏置当 `Dense` 的值成员,构造的时候拷进来:

```cpp
template <std::size_t In, std::size_t Out>
struct Dense {
    Tensor<Out, In> weight_;   // 值成员,自带存储
    Vector<Out>     bias_;
};
```

这么写,一个 `Dense` 实例自带 `Out*In + Out` 个 float 的存储。听着没啥,直到你算一下 `sizeof`。拿 MNIST 第一层 `Dense<784, 128>` 量级(本机 g++ 16.1 实测):

```text
sizeof(Dense<784,128>) [值成员版] = 401920 bytes (392.5 KB)
```

一个 Dense 实例自带近 400KB。这意味着实例走到哪、这 400KB 就跟到哪:当全局变量它常驻 `.bss` 一份,当局部变量它被拷上栈(MCU 上几 KB 的栈直接爆),`new` 到堆上又违反 v0.1 禁堆。三条路都堵。

你可能会想:构造的时候我用 `std::move` 把权重 move 进来,总不该拷了吧。实测打脸——`std::array<float, N>` 的移动构造是逐元素 move,而 `float` 这种基本类型没有独立的 move 语义(float 的 move 构造就是拷贝构造)。所以 move 一个大 float 权重,等于实打实拷 `Out*In` 次,move 在这儿一分钱不省。这个点拿一个带 move 计数器的类型实测,move `array<MoveTrk, 1000>` 会触发正好 1000 次元素 move,佐证得清清楚楚。

所以正确的方向是:**`Dense` 不自带权重存储,只持一个指向外部存储的视图**。视图用 `std::span`(固定 extent,编译期知道大小),权重数据外部维护:

```cpp
template <std::size_t In, std::size_t Out>
struct Dense {
    std::span<const float, Out * In> weight_;   // 视图,指向外部
    std::span<const float, Out>     bias_;
};
```

同样的 `Dense<784, 128>`,`sizeof` 掉到:

```text
sizeof(Dense<784,128>) [span 版] = 16 bytes
```

16 字节,两个固定 extent 的 span(各 8 字节,只存一个指针,extent 是模板参数编译期已知、不必存长度)。实例随便拷、随便放栈,毫无压力。

这条决策真正值钱的地方在 Stage 5。权重的本质是"训练好的常量",最终形态是 `inline constexpr std::array`(烤进 `.rodata`,MCU 上能放 Flash 不占 RAM)。span 方案让 `Dense` 接口现在能接测试用的局部 Tensor,Stage 5 把外部存储换成全局 `inline constexpr std::array` 时,`Dense` 的接口和 `forward` 一行不改——它本来就只看 span,不在乎外部存储是局部还是全局常量。反过来,如果 Stage 2 用值成员,Stage 5 就得把成员类型从 `Tensor` 推翻成 span,接口动一遍。所以 span 不是"提前干 Stage 5 的活",而是"让 Stage 2 的接口能零成本演进到 Stage 5"。这条接口契约笔者在这里钉死:

| | 外部存储 | Dense 实例 | constexpr |
|---|---|---|---|
| **Stage 2(测试)** | 局部 `Tensor` | 存它的 `view()` | 封进 constexpr 函数验证 |
| **Stage 5(部署)** | 全局 `inline constexpr std::array` | 存它的 `view()` | `constexpr Dense layer(gw,gb)` 合法,全链路通 |

代价要交代清楚。span 是非拥有视图,`Dense` 不再自己管权重的生命期——外部存储必须活得比 `Dense` 久。测试里这意味着构造 `Dense` 用的 Tensor 不能是临时量(`Dense(Tensor{...}临时, ...)` 析构后 span 悬垂),得提成具名变量。目标场景(权重是全局 constexpr 常量)天然满足,这条约束只在测试里多两行代码。顺带的,Dense 不再有默认构造——span 没有有意义的默认状态,默认构造的 Dense 没 bind 权重、不能 forward,这种"空 Dense"在业务上也没意义。

### 决策二:权重形状 `[Out, In]`

完整论证见 [02 篇](./02-weight-shape.md),这里只复述结论:算第 o 个输出要读权重第 o 行,行主序下一行连续、cache 配合(1024×1024 实测行序比列序快 7 倍);同时跟 PyTorch `nn.Linear` 的 `(out, in)` 惯例对齐,Stage 5 对拍零摩擦。`Dense` 里权重 span 的 extent 写成 `Out * In`,下标展开 `o * In + i`。

### 决策三:`forward` 类内 inline,不返回 `std::expected`

两个子点。

第一,`forward` 必须写在类里(inline),不能声明在头、定义在类外。这是模板方法 + `constexpr` 的双重要求:模板方法本就只能 header-only,`constexpr` 更要求定义在编译点可见。声明/定义分离会让 constexpr 求值报 `used before its definition`,放 `.cpp` 会报 `used but never defined`。本机实测踩过,直接把体写进类里就没事。

第二,`forward` 不走 `std::expected`。Stage 1 的 `Tensor::at` 用 expected 是因为越界是运行期才知道的事,得带错误信息回来。但 `Dense` 的形状在模板参数里锁死了——`forward(const Vector<In>&)` 接的输入维度由类型保证,运行期根本没有"形状错"这种错误可报。这是 [Stage 1 的 05 篇](../stage1/05-shape-in-type.md)"形状塞进类型"的回报:一整类形状错误被挪到编译期,`forward` 只管算、不操心检查。

## 实现指引

### 接口草图(跟工程代码对齐)

工程里就一个头 `include/tinyml/dense.hpp`,签名长这样:

```cpp
#pragma once

#include <cstddef>
#include <span>

#include "tinyml/tensor.hpp"

namespace tamcpp::tinyml {

template <std::size_t In, std::size_t Out, typename StorageType = float>
struct Dense {
    static constexpr std::size_t kIn = In;
    static constexpr std::size_t kOut = Out;

    static_assert(In > 0 && Out > 0, "We haven't seen a tensor with 0 size");

    using Bias_t      = Vector<Out, StorageType>;
    using BiasView_t  = std::span<const StorageType, Out>;
    using Weight_t    = Tensor<Out, In, StorageType>;
    using WeightView_t = std::span<const StorageType, Out * In>;

    // 接 Tensor,内部只存它的 view —— 零拷贝。
    // weight / bias 必须活得比 Dense 久(span 是非拥有视图)。
    constexpr Dense(const Weight_t& weight, const Bias_t& bias) noexcept
        : weight_(weight.view()), bias_(bias.view()) {}

    // y = W·x + b。形状编译期锁死,无需 std::expected。
    constexpr Vector<Out, StorageType>
    forward(const Vector<In, StorageType>& vec_in) const noexcept {
        Vector<Out, StorageType> result{};
        for (std::size_t vec_out_index = 0; vec_out_index < Out; ++vec_out_index) {
            StorageType& acc = result(0, vec_out_index); // 取引用,省掉每次 += 的下标重算
            acc = bias_[vec_out_index];
            for (std::size_t vec_in_index = 0; vec_in_index < In; ++vec_in_index) {
                acc += vec_in(0, vec_in_index) * weight_[vec_out_index * In + vec_in_index];
            }
        }
        return result;
    }

    constexpr std::span<const StorageType, Out * In> weight() const noexcept { return weight_; }
    constexpr std::span<const StorageType, Out>      bias()    const noexcept { return bias_; }

  private:
    WeightView_t weight_;
    BiasView_t bias_;
};

} // namespace tamcpp::tinyml
```

几个容易看走眼的地方点一下。

模板参数顺序是 `<In, Out, StorageType = float>`,**输入维度在前**。这跟 `Tensor<Rows, Cols>` 的"行在前"是两套语义:Tensor 的 Rows/Cols 是存储形状,Dense 的 In/Out 是层的数据流方向(进多少、出多少)。`static constexpr` 的 `kIn`/`kOut` 是给 `layer.kIn` 这种点访问用的镜像,值就是模板参数本身。

`forward` 的双循环结构,外层走输出 `vec_out_index`、内层走输入 `vec_in_index`,是 [02 篇](./02-weight-shape.md)那条 cache 红利的落地:内层顺着 `weight_` 的一行连续读。累加器 `acc` 先用 `bias_[o]` 起手再 `+=`,省掉一次单独的归零;取 `StorageType& acc` 引用绑定到 `result` 的元素,省掉每次 `+=` 都过一遍 `operator()` 的下标重算。`x` 那边用 `vec_in(0, vec_in_index)` 是因为 `Vector<In>` 就是 `Tensor<1, In>`,行下标固定 0([01 篇](./01-what-is-dense.md)提过,行下标写非 0 会越过 `std::array` 边界,运行期 UB、编译期还会让 constexpr 求值失败)。

`weight()` / `bias()` 返回 span 的拷贝(span 本身就两三个字,拷廉价),给测试和 Stage 4 串联时查形状用。

### CMake:把 `dense.hpp` 挂进 INTERFACE 源列表

顶层 `CMakeLists.txt` 的 INTERFACE 库要把新头加上(Stage 1 只挂了 `tensor.hpp`):

```cmake
add_library(TAMCPP_TinyML INTERFACE
    include/tinyml/tensor.hpp
    include/tinyml/dense.hpp)
```

头文件靠 `target_include_directories` 能被找到,但 INTERFACE 的 `SOURCES` 不列它,CMake 会警告、IDE 也不认。测试 target 的注册照 Stage 1 的糖,`tests/CMakeLists.txt` 加一行:

```cmake
tamcpp_add_test(dense_api dense_api.cpp)
```

## 验证

`tests/dense_api.cpp` 的几个 case 是 Stage 2"真过了"的判据,尤其行主序那条,是 Stage 5 对拍的前置:

```cpp
TEST_CASE("dense dims are compile-time visible", "[dense]") {
    Tensor<2, 3> w(std::array{0.f, 0.f, 0.f, 0.f, 0.f, 0.f}); // [Out=2, In=3]
    Vector<2> b(std::array{0.f, 0.f});
    Dense<3, 2> layer(w, b);                                   // In=3 -> Out=2
    static_assert(layer.kIn == 3);
    static_assert(layer.kOut == 2);
}

TEST_CASE("dense forward matches hand-computed matvec", "[dense]") {
    // W[Out=2, In=2] = [[1,2],[3,4]], x=[5,6], b=[10,20]
    // y[0] = 1*5 + 2*6 + 10 = 27
    // y[1] = 3*5 + 4*6 + 20 = 59
    Tensor<2, 2> w(std::array{1.f, 2.f, 3.f, 4.f});
    Vector<2> b(std::array{10.f, 20.f});
    Dense<2, 2> layer(w, b);
    Vector<2> x(std::array{5.f, 6.f});
    auto y = layer.forward(x);
    REQUIRE(y(0, 0) == 27.f);
    REQUIRE(y(0, 1) == 59.f);
}

TEST_CASE("dense weight storage is row-major [Out, In]", "[dense]") {
    Tensor<2, 3> w(std::array{1.f, 2.f, 3.f,   // o=0 这一行
                              4.f, 5.f, 6.f}); // o=1 这一行
    Vector<2> b(std::array{0.f, 0.f});
    Dense<3, 2> layer(w, b);                   // In=3, Out=2
    REQUIRE(layer.weight()[1 * 3 + 0] == 4.f); // 第1个输出的第0个权重
}

TEST_CASE("dense zero-weight layer forward is zero", "[dense]") {
    Tensor<2, 2> w(std::array{0.f, 0.f, 0.f, 0.f}); // 显式全零(无默认构造)
    Vector<2> b(std::array{0.f, 0.f});
    Dense<2, 2> layer(w, b);
    Vector<2> x(std::array{1.f, 2.f});
    auto y = layer.forward(x);
    REQUIRE(y(0, 0) == 0.f);
    REQUIRE(y(0, 1) == 0.f);
}

// span 持指针,持久化进静态 constexpr 变量 g++ 会报 "incompletely initialized
// variable"。封进 constexpr 函数让指针不逃逸出帧。Stage 5 换全局 inline constexpr 后,
// 才能直接写 constexpr Dense layer(global_w, ...)。见常见坑 2。
constexpr bool dense_forward_is_constexpr() {
    Tensor<2, 2> w(std::array{1.f, 2.f, 3.f, 4.f});
    Vector<2> b(std::array{0.f, 0.f});
    Dense<2, 2> layer(w, b);
    Vector<2> x(std::array{1.f, 1.f});
    auto y = layer.forward(x);
    return y(0, 0) == 3.f && y(0, 1) == 7.f; // 1*1+2*1=3, 3*1+4*1=7
}

TEST_CASE("dense forward is constexpr-evaluable", "[dense]") {
    static_assert(dense_forward_is_constexpr());
}
```

```bash
cmake -S . -B build && cmake --build build -j
ctest --test-dir build
```

本机实测,12 个 case 全绿(Stage 1 拷来的 tensor/smoke 6 个 + Stage 2 的 dense 5 个 + smoke 1 个):

```text
100% tests passed out of 12
Total Test time (real) = 0.05 sec
```

5 个 dense case 全绿就算过。手算 matvec 那条(27、59)验证 `forward` 算得对;行主序那条验证权重布局跟 [02 篇](./02-weight-shape.md)钉的一致;constexpr 那条验证 `forward` 真能编译期求值。

## 常见坑

1. **`forward` 必须类内 inline**(实测):模板方法 + `constexpr` 双重要求。声明在头、定义在类外会让 constexpr 求值报 `used before its definition`;放 `.cpp` 会报 `used but never defined`。直接把体写进类里,别分离。这是 span 版 forward 跟 Stage 1 Tensor 不一样的地方——Tensor 的方法体都很短、类内写天然顺,Dense 的 forward 有循环体,容易让人想抽到类外,别抽。
2. **constexpr 不能持久化局部 span**(实测):`constexpr Dense layer(局部 w, 局部 b)` g++ 报 `not a constant expression / incompletely initialized variable`。根因:span 内部是指针,函数内局部 constexpr 对象的地址**持久化进静态 constexpr 变量**不合法。规避:封进 constexpr 函数(指针不逃逸出帧),用 `static_assert(func())` 验证。Stage 5 权重换成全局 `inline constexpr std::array`(静态存储期 + 编译期可寻址)后,这条限制自然解除——这恰好是 span + inline constexpr 的契合点。
3. **循环变量用 `std::size_t` 不是 `int`**:`for (int i = 0; i < Out; ...)` 跟 `std::size_t` 比,`-Wsign-compare` 会警告(实测 `-Wall -Wextra` 下冒两条)。测试能过是因为值都是小正数,提升成 unsigned 后碰巧对;但 `Out > INT_MAX` 时 `int` 会溢出(类型上就不该拿有符号索引无符号维度)。跟 [Stage 1 的 Tensor](../stage1/06-tensor.md) 一样,循环变量老老实实 `std::size_t`。
4. **权重下标是 `o*In + i`,不是 `i*Out + o`**:span 一维,行主序展开是 `o*In + i`。写反成 `i*Out + o`,当 `Out == In`(2×2 测试)时形状碰巧不报错,但算的是 Wᵀ·x。所以测试特意配了 `2×3` 非方阵 case(`Dense<3, 2>`),方阵掩盖不了。
5. **bias 漏加 / 没用 bias 起手**:只累加 `W·x` 忘了 bias,手算 case 差一个偏置(27 变 17)。顺手的写法是 `acc = bias_[o]` 起手再累加(本工程这么写),省一次单独归零。
6. **头文件卫生**:`dense.hpp` 要进 INTERFACE 的 `SOURCES`(坑 1.4 同类);新建头别忘 `#pragma once`,用到的 `<cstdio>` 之类要显式 include(别靠间接带进来,换编译器/标准库版本就悬);测试文件别重复 include 同一个头(`#pragma once` 挡住重定义所以没炸,但 lint 会报)。Stage 2 工程还该有个 `.gitignore` 忽略 `build*/`(Stage 0 有,从 Stage 1 拷过来的目录容易漏),不然 `build/` 整个进 git。

## 到这里,Stage 2 齐了

回头看 Stage 2 这三篇:[01](./01-what-is-dense.md) 把 Dense 公式拆成 Out 个加权和,[02](./02-weight-shape.md) 把权重布局 `[Out, In]` 的 cache 账和 PyTorch 对齐钉死,这篇把 `Dense` 用 span 视图造出来。`forward` 算的是仿射,激活留给 Stage 3。

下一步 Stage 3 会造 ReLU(把负数归零)和 Argmax(挑最大值的位置),有了这两个,Stage 4 就能把两层 Dense 夹一个 ReLU 接成完整 MLP,Argmax 在末尾吐分类结果。Span 视图这套存储策略到时候会继续发挥作用——Stage 5 把训练好的权重烤成 `inline constexpr std::array`,`Dense` 接口一行不改就能接上。
