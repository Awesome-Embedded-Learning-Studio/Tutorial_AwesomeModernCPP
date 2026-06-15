// Standard: C++26
// 新标准容器：flat_map(C++23 拍平排序 vector) / inplace_vector(C++26 定容不堆分配) / mdspan(C++23
// 多维视图)
#include <cstdio>
#include <flat_map>
#include <inplace_vector>
#include <mdspan>
#include <string>

int main() {
    std::printf("== flat_map（C++23）：底层排序 vector，find O(log n) 且 cache 友好 ==\n");
    std::flat_map<int, std::string> fm;
    fm.insert({3, "three"});
    fm.insert({1, "one"});
    fm.insert({2, "two"}); // O(n)：维护有序要搬移
    auto it = fm.find(2);  // O(log n)：二分查找
    std::printf("find(2) = %s\n", it->second.c_str());
    std::printf("有序遍历：");
    for (auto [k, v] : fm) {
        std::printf("%d:%s ", k, v.c_str());
    }
    std::printf("\n");

    std::printf("\n== inplace_vector（C++26）：容量编译期定死 N，元素存对象内，绝不堆分配 ==\n");
    std::inplace_vector<int, 8> ipv;
    for (int i = 1; i <= 5; ++i) {
        ipv.push_back(i);
    }
    std::printf("size = %zu, capacity = %zu（无 new，放栈/静态区）\n", ipv.size(), ipv.capacity());

    std::printf("\n== mdspan（C++23）：一维内存的多维视图，m[i,j] 多维下标（P2128）==\n");
    int raw[12] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    // 把 12 个 int 当 3 行 4 列（行优先）
    std::mdspan<int, std::extents<std::size_t, 3, 4>> m(raw);
    std::printf("m[1,2] = %d  m[2,3] = %d  rank = %zu\n", m[1, 2], m[2, 3], m.rank());
    return 0;
}
