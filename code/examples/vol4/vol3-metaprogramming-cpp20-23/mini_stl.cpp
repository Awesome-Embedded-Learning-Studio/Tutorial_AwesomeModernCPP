// 配套 09-mini-stl-with-concepts.md
// 用 C++20 concepts 约束的 mini-STL 算法库:transform / accumulate / find_if
//
// 编译运行:
//   g++ -std=c++20 -Wall -Wextra mini_stl.cpp -o mstl && ./mstl
// 看传错类型的报错(NoPlus 不满足 Addable,concept 报错点名约束):
//   g++ -std=c++20 -Wall -Wextra -DSHOW_ACC_ERROR mini_stl.cpp
#include <concepts>
#include <functional>
#include <iostream>
#include <iterator>
#include <ranges>
#include <string>
#include <vector>

namespace my {

// 自定义 concept:支持 a + b 且结果能转成 U
template <typename T, typename U = T>
concept Addable = requires(T a, U b) {
    { a + b } -> std::convertible_to<U>;
};

// 自定义 concept:能用 < 比较
template <typename T>
concept Ordered = requires(T a, T b) {
    { a < b } -> std::convertible_to<bool>;
};

// transform:把 range 经 func 变换后写到 out
template <std::ranges::input_range R, typename Out, typename F>
    requires std::output_iterator<Out, std::ranges::range_value_t<R>> &&
             std::invocable<F&, std::ranges::range_reference_t<R>>
Out transform(R&& r, Out out, F f) {
    for (auto&& x : r) {
        *out++ = std::invoke(f, x);
    }
    return out;
}

// accumulate:累加 range 的元素到 init
template <std::ranges::input_range R, typename T>
    requires Addable<T, std::ranges::range_value_t<R>>
T accumulate(R&& r, T init) {
    for (auto&& x : r) {
        init = init + x;
    }
    return init;
}

// find_if:找第一个满足 pred 的元素
template <std::ranges::input_range R, typename Pred>
    requires std::predicate<Pred&, std::ranges::range_reference_t<R>>
std::ranges::borrowed_iterator_t<R> find_if(R&& r, Pred pred) {
    for (auto it = std::ranges::begin(r); it != std::ranges::end(r); ++it) {
        if (std::invoke(pred, *it))
            return it;
    }
    return std::ranges::end(r);
}

} // namespace my

#ifdef SHOW_ACC_ERROR
struct NoPlus {}; // 没有 operator+
int main() {
    std::vector<NoPlus> v(3);
    my::accumulate(v, NoPlus{}); // Addable 约束不满足,concept 报错
}
#else
int main() {
    std::vector<int> v = {1, 2, 3, 4};

    std::vector<int> squared;
    my::transform(v, std::back_inserter(squared), [](int x) { return x * x; });
    std::cout << "transform 平方: ";
    for (int x : squared)
        std::cout << x << " ";
    std::cout << "\n";

    std::cout << "accumulate 求和:  " << my::accumulate(v, 0) << "\n";

    std::vector<std::string> words = {"a", "b", "c"};
    std::cout << "accumulate 拼接:  " << my::accumulate(words, std::string("start:")) << "\n";

    auto it = my::find_if(v, [](int x) { return x % 2 == 0; });
    if (it != v.end())
        std::cout << "find_if 第一个偶数: " << *it << "\n";
}
#endif
