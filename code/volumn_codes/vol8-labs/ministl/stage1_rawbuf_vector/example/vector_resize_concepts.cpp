// 例程:resize 的 concepts 约束 —— "缺默认构造"在编译期就被拦下,报错说人话。
// 想亲眼看报错:删掉下面这行 NO_ERROR 再编译。
#define NO_ERROR 1
#include <cstdio>

#include "tamcpp_ministl/vector.hpp"

namespace {
struct NoDefault {
    explicit NoDefault(int v) : v(v) {}
    int v;
};
} // namespace

int main() {
    using tamcpp::ministl::Vector;

    Vector<int> v{1, 2, 3};
    v.resize(5); // int 有默认构造,正常放行
    std::printf("resize(5) OK: size=%zu, v[4]=%d\n", v.size(), v[4]);

#if NO_ERROR
    // 打开下面三行,看 concepts 把违约拦在编译期:
    // Vector<NoDefault> nd;
    // nd.emplace_back(1);
    // nd.resize(2);
#else
    Vector<NoDefault> nd;
    nd.emplace_back(1);
    nd.resize(2);
#endif
    return 0;
}
