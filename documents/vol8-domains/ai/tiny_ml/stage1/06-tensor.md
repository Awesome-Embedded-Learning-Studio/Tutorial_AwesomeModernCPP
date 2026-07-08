---
title: "固定维度 Tensor——推理器的数据底座"
description: "造一个编译期固定维度、行主序、std::array 存储的 Tensor<Rows,Cols,StorageType>,at 走 std::expected 返回值做无异常错误处理。三个设计决策:at 返回值不返回引用、固定 2D、行主序。配套工程 code/volumn_codes/vol8-labs/ai/tiny_ml/stage1/"
chapter: 8
order: 12
platform: host
difficulty: advanced
cpp_standard: [23]
reading_time_minutes: 12
prerequisites:
  - "工程脚手架——把工具链地基浇好"
  - "模板与非类型参数"
tags:
  - host
  - cpp-modern
  - advanced
  - 模板
  - 内存管理
  - 类型安全
---

# 固定维度 Tensor——推理器的数据底座

Stage 1 要造的是整个推理器的数据底座:一个编译期固定维度、行主序、`std::array` 存储的 `Tensor<Rows, Cols, StorageType>`,外加一套基于 `std::expected` 的无异常错误处理。做完这一 stage,你能用 `Tensor<1, 3>` 表达传感器输入、`Tensor<4, 3>` 表达 Dense 权重,但 Dense 本身留到 Stage 2 再写。配套工程在 `code/volumn_codes/vol8-labs/ai/tiny_ml/stage1/`。

这篇是 Stage 1 的实现主文档。如果你对 Tensor 这个概念本身、或者为什么非得自己造一个还陌生,先读[引入五篇](./index.md)再回来;已经清楚 Tensor 是什么、为什么这么设计的,这篇往下看就行。

## 为什么先造 Tensor

Stage 1 把 v0.1 硬约束里最承重的三条凑在一起:无堆分配、不用 `std::vector`、编译期固定大小。Tensor 一旦定下来,Stage 2 的 Dense 就只是拿两个 Tensor 做乘加,Stage 5 的 NumPy 导出就只是往 `inline constexpr std::array` 里填数。Tensor 的布局是后面 Python 和 C++ 对拍的基础,这根线必须在 Stage 1 定下来,否则 Stage 5 的 golden test 永远对不上。

## 三个要先拍板的设计决策

### 决策一:at 返回 `std::expected` 的值,不返回引用

推理热路径用 `operator()`(返回引用、不检查,越界是 UB,要的是快)。但偶尔会在不确定边界的地方安全访问一个元素——越界了得带错误信息回来,不能 UB。这个“带检查版”的活,交给 `at`。

`at` 的返回类型用 `std::expected<StorageType, Error>`(C++23)。第一反应可能是:返回引用才对吧,`std::array::at` 不就返回引用?但这里撞了 C++23 的一个硬约束:**`std::expected<T, E>` 不接受 T 是引用类型**。`std::expected<T&, E>` 在标准里直接 `static_assert(!is_reference_v<T>)`,编译不过(本机 g++ 16.1 实测会报一连串错,根因就是这条)。

所以“返回引用 + 走 expected 错误”这条路,标准层面就堵死了。剩下三条妥协:返回指针(`expected<T*, Error>`)、包一层 `reference_wrapper`、或者干脆返回值。前两个要么接口丑(解引用多一步),要么多包一层对小白不好讲。最后选返回值:`at` 拿到的是元素的拷贝,越界走 expected 的错误路径。

代价是 `at` 改不了原元素——`t.at(0,0).value() = 5.f` 改的是 expected 里的临时拷贝,原元素不动。但这是设计意图:`at` 在咱们 Lab 里定位是“带检查的安全读取”,要改元素走 `operator()`。推理热路径全是 `operator()`,`at` 只是测试和校验时用,返回值够用,这点代价换不来什么收益去推翻它。

### 决策二:固定 2D,不做可变模板

维度进类型有什么好处,[引入 05 篇](./05-shape-in-type.md)讲过了,这里只讨论进了类型之后,做固定 2D 还是可变 N 维。v0.1 的 MLP 全程只有两种形态:行向量(输入、输出、偏置)和 2D 矩阵(权重)。一个固定 2D 的 `Tensor<Rows, Cols, StorageType>` 就全覆盖了,没必要上可变模板 `Tensor<StorageType, Dims...>`。那东西写起来陡峭,MLP 又根本用不上 3D+,属于为不存在的要求付复杂度税。1D 向量直接用别名 `Vector<Cols> = Tensor<1, Cols>`,Argmax 和 Dense 的输入输出都走它,语义统一。

### 决策三:`std::array` 存储 + 行主序

存储没得选,硬约束禁了 `std::vector`,只剩 `std::array`,三个候选的过堂见[引入 03 篇](./03-why-not-built-in.md)。布局走**行主序** `internals_[i*Cols + j]`,跟 NumPy 默认的 C order 一致,这样 Stage 5 的 Python 权重和 C++ Tensor 能一位对一位地对拍,Python 的 `W[i, j]` 和 C++ 的 `W(i, j)` 指向同一个数。行主序的完整推导和内存图在[引入 04 篇](./04-row-major.md),这里不重复。

## 实现指引

### 接口草图(跟工程代码对齐)

工程里就一个头 `include/tinyml/tensor.hpp`,签名长这样(实现见工程,这里讲要点):

```cpp
#pragma once
#include <array>
#include <cstddef>
#include <expected>
#include <span>

namespace tamcpp::tinyml {

template <std::size_t Rows, std::size_t Cols, typename StorageType = float>
class Tensor {
  public:
    // 错误码挂类内:每个维度的 Tensor 各带一份,够用不折腾
    enum class Error { kShapeMismatch, kOutOfRange };

    // 带检查的安全读取:越界走 expected 错误,不抛异常
    constexpr std::expected<StorageType, Error>
    at(std::size_t i, std::size_t j) noexcept {
        if (i >= Rows || j >= Cols) return std::unexpected{Error::kOutOfRange};
        return internals_[i * Cols + j];
    }
    constexpr std::expected<const StorageType, Error>
    at(std::size_t i, std::size_t j) const noexcept;   // 同上,const 版

    static_assert(Rows > 0 && Cols > 0, "dims must be positive");

    constexpr Tensor() = default;
    constexpr Tensor(std::array<StorageType, Rows * Cols> internals)
        : internals_(std::move(internals)) {}

    constexpr std::size_t row() const noexcept { return Rows; }
    constexpr std::size_t col() const noexcept { return Cols; }
    constexpr std::size_t size() const noexcept { return internals_.size(); }

    // 热路径访问:返回引用、不检查,越界 UB
    constexpr StorageType&       operator()(std::size_t i, std::size_t j) noexcept;
    constexpr const StorageType& operator()(std::size_t i, std::size_t j) const noexcept;

    // 扁平 span 视图(Stage 2 Dense 读权重用,不拷贝)
    constexpr std::span<const StorageType, Rows * Cols> view() const noexcept;
    constexpr std::span<StorageType,       Rows * Cols> view()       noexcept;

    constexpr std::array<StorageType, Rows * Cols>&       storage()       noexcept;
    constexpr std::array<const StorageType, Rows * Cols>& storage() const noexcept;

  private:
    std::array<StorageType, Rows * Cols> internals_{};   // 这个 {} 不能省,见常见坑
};

template <std::size_t Cols, typename StorageType = float>
using Vector = Tensor<1, Cols, StorageType>;

} // namespace tamcpp::tinyml
```

几个容易看走眼的地方点一下。

模板参数顺序是 `<Rows, Cols, StorageType = float>`,**维度在前、类型有默认值**。所以写 `Tensor<4, 3>` 就是 `Tensor<4, 3, float>`——维度才是 Tensor 身份的核心,放前面,类型默认 float 能省一半书写。

`row()` / `col()` / `size()` 用 `constexpr` 就够(`static_assert(tensor.size() == 12)` 能过),不必非要 `consteval`。`constexpr` 允许运行期也调,宽松一点;真要限定编译期再上 `consteval` 也不迟。

`at` 是 `noexcept` 的——它走 expected 的错误路径,不抛异常,符合核心路径无异常的约束。注意它返回值不是引用,理由见决策一。

**构造函数为什么没标 noexcept。** `at`、`operator()` 这些函数标了 `noexcept`,默认构造 `Tensor() = default` 和那个接 `std::array` 的构造函数却没标。差别在:访问类函数只做下标比较和取元素(对 float,拷贝不抛),确实不抛,标 `noexcept` 没风险;构造函数要真的去构造成员 `internals_{}`,这步抛不抛,得看 StorageType 自己的构造。所以构造的 `noexcept` 跟 StorageType 挂钩,这里干脆交给编译器——`= default` 会按“成员构造抛不抛”自动配上隐式异常说明,本机 g++ 16.1 实测 `Tensor<4, 3, float>` 的默认构造被推成 `noexcept(true)`。没把 noexcept 写在脸上,不等于会抛;对 float 它就是不抛,只是让编译器替你认下这件事。源码里 `// Q: why not noexcept?` 那句问的就是它。

### CMake:INTERFACE 库 + 跨编译器警告封装

工程顶层 `CMakeLists.txt` 把推理库声明成 `INTERFACE`(纯头库,`Tensor` 全 inline 在头里,没有 `.cpp` 产物):

```cmake
add_library(TAMCPP_TinyML INTERFACE include/tinyml/tensor.hpp)
target_include_directories(TAMCPP_TinyML INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
```

警告标志跨编译器不通用(MSVC 一套、GCC/Clang 一套),封一个函数复用,别在每个 target 里抄 generator-expression:

```cmake
function(tamcpp_target_warnings target)
    target_compile_options(${target} PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4;/permissive-;/Zi>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall;-Wextra;-Wpedantic;-g>
    )
endfunction()
```

测试 target 同样封一层糖,`tests/CMakeLists.txt` 每个测试一行注册:

```cmake
function(tamcpp_add_test name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE Catch2::Catch2WithMain TAMCPP_TinyML)
    tamcpp_target_warnings(${name})
    catch_discover_tests(${name})
endfunction()

tamcpp_add_test(smoke_catch2 smoke.cpp)
tamcpp_add_test(tensor_api  tensor_api.cpp)   # ← 这个别漏,见常见坑 5
```

还有一句容易忘的:顶层要 `enable_testing()`,否则 `catch_discover_tests` 注册的 `add_test` 全悬空,`ctest` 永远 "No tests found"。

## 验证

`tests/tensor_api.cpp` 的几个 case 是 Stage 1 “真过了”的判据,尤其行主序那条,是 Stage 5 对拍的前置:

```cpp
TEST_CASE("dims are compile-time visible", "[tensor]") {
    Tensor<4, 3> tensor;
    static_assert(tensor.size() == 12);
    static_assert(tensor.row() == 4);
    static_assert(tensor.col() == 3);
}

TEST_CASE("construct and access", "[tensor]") {
    Tensor<2, 2> t(std::array{1.f, 2.f, 3.f, 4.f});
    REQUIRE(t(1, 0) == 3.f);
}

TEST_CASE("row-major layout matches flat storage", "[tensor]") {
    Tensor<2, 2> t(std::array{1.f, 2.f, 3.f, 4.f});
    for (std::size_t i = 0; i < 2; ++i)
        for (std::size_t j = 0; j < 2; ++j)
            REQUIRE(t(i, j) == t.storage()[i * 2 + j]);
    REQUIRE(t.view().front() == t.storage()[0]);
}

TEST_CASE("vector is a row tensor", "[tensor]") {
    Vector<3> v;
    static_assert(v.row() == 1 && v.col() == 3);
}

TEST_CASE("out-of-range goes through expected", "[tensor]") {
    Tensor<2, 2> t;
    auto r = t.at(99, 99);
    REQUIRE_FALSE(r);
    REQUIRE(r.error() == Tensor<2, 2>::Error::kOutOfRange);

    // 单维越界也要拦(防 && 回归:i 越界但 j 在范围内)
    auto r_single = t.at(99, 0);
    REQUIRE_FALSE(r_single);
    REQUIRE(r_single.error() == Tensor<2, 2>::Error::kOutOfRange);
}

TEST_CASE("default construction is zero-initialized", "[tensor]") {
    Tensor<2, 2> t;
    REQUIRE(t(0, 0) == 0.f);
}
```

```bash
cmake -S . -B build && cmake --build build -j
ctest --test-dir build
```

6 个 case 全绿就算过。行主序那条尤其要看,它定下了和 NumPy 的对齐关系,这条过不了,Stage 5 的对拍就没了基础。

## 常见坑

1. **at 越界检查用 `||` 不是 `&&`**:`if (i >= Rows && j >= Cols)` 要求 i、j **都**越界才报错,单维越界(`at(99, 0)`)直接漏过去访问 `internals_[198]`,撞 `std::array` 越界断言。写成 `||`,i 或 j 任一越界就返回错误。这条本机 ASAN 实证过。
2. **`internals_{}` 的 `{}` 不能省**:成员没写 `{}` 时,`Tensor<2,2> t;` 的 default 构造让 `std::array` 元素处于 indeterminate,`t(0, 0)` 读的是未初始化垃圾(UB)。测试能过是栈上垃圾凑巧是 0,撞大运。本机 msan 实证 `use-of-uninitialized-value`。加上 `{}` 才 value-init(float 就是 0.0f),default 构造名副其实。
3. **`std::expected` 不接引用类型**:`expected<T&, E>` 编译不过(标准 `static_assert(!is_reference_v<T>)`)。所以 at 没法“返回引用 + 走 expected 错误”,只能返回值或指针。咱们选值,理由见决策一。
4. **CTAD 会丢维度**:`Tensor t(std::array{...})` 这种类模板参数推导会把 `Rows` / `Cols` 全丢了,必须显式写 `Tensor<2, 2>`。别指望 CTAD 帮你。
5. **`tests/CMakeLists.txt` 要注册测试 target**:写了 `tensor_api.cpp` 但忘了 `tamcpp_add_test(tensor_api tensor_api.cpp)`,构建根本不会编译它,你跑的 `ctest` 只有 smoke。新加测试文件记得在这儿注册一行。
