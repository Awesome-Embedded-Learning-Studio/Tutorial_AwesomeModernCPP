#include "catch2/catch_test_macros.hpp"
#include "tinyml/tensor.hpp"
#include <catch2/catch_test_macros.hpp>

using namespace tamcpp::tinyml;

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
    for (std::size_t i = 0; i < 2; ++i) {
        for (std::size_t j = 0; j < 2; ++j) {
            REQUIRE(t(i, j) == t.storage()[i * 2 + j]);
        }
    }
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
