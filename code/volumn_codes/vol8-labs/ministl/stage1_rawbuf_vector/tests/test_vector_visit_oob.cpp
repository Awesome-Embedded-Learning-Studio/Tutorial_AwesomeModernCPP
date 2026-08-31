// tests/test_vector_visit_oob.cpp —— Vector::visit_at 的界外死法
//
// 界必须是 size 不是 capacity:capacity 故意给足(8)、size 只装 3,
// 访问第 5 格 —— 若界错查成容量,这道题就被骗过去了。
#include "tamcpp_ministl/vector.hpp"

int main() {
    tamcpp::ministl::Vector<int> v;
    v.reserve(8);
    for (int i = 0; i < 3; ++i)
        v.push_back(i);
    v.visit_at(5); // 5 < capacity(8),但 5 >= size(3)
    return 0;      // 真跑到这行 = 检查没拦住
}
