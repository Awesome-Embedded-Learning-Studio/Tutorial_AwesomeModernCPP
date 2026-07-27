#include "catch2/catch_test_macros.hpp"
#include "tinyml/dense.hpp"
#include "tinyml/tensor.hpp"
#include <array>

using namespace tamcpp::tinyml;

TEST_CASE("dense dims are compile-time visible", "[dense]") {
    Tensor<2, 3> w(std::array{0.f, 0.f, 0.f, 0.f, 0.f, 0.f}); // [Out=2, In=3]
    Vector<2> b(std::array{0.f, 0.f});
    Dense<3, 2> layer(w, b); // In=3 -> Out=2
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
    // 承接 stage1 行主序:weight_[o*In + i] 连续
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
