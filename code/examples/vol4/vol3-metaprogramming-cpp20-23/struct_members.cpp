// 配套 06-static-reflection-basics.md「实战一:遍历 struct 的成员」
// ⚠️ 本机 GCC16/clang22 不支持 P2996(C++26 反射)。
//    在 Godbolt 选 clang_bb_p2996(Bloomberg 实验分支)编译运行,参数 -std=c++2c -freflection-latest
#include <iostream>
#include <meta>

struct Point {
    int x;
    int y;
};

int main() {
    using namespace std::meta;
    constexpr auto refl = ^^Point;
    constexpr auto ctx = access_context::current();
    // define_static_array 把 vector 物化到静态存储,template for 才能编过
    constexpr auto members = define_static_array(nonstatic_data_members_of(refl, ctx));

    std::cout << identifier_of(refl) << "\n";
    template for (constexpr auto member : members) {
        std::cout << "  " << identifier_of(member) << "\n";
    }
}
