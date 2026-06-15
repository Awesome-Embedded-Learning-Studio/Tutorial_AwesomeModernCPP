// Standard: C++20
// unordered_map：哈希桶、rehash 触发的质数桶序列、load_factor、reserve 预撑桶
#include <iostream>
#include <unordered_map>

int main() {
    std::cout << "== rehash 触发质数桶序列（bucket_count 跳变）==\n";
    std::unordered_map<int, int> m;
    std::size_t last = m.bucket_count();
    std::cout << "初始 bucket_count = " << last << '\n';
    for (int i = 0; i < 200; ++i) {
        m[i] = i;
        if (m.bucket_count() != last) {
            std::cout << "插入 " << i << " 后 rehash: " << last << " -> " << m.bucket_count()
                      << "（load_factor=" << m.load_factor() << "）\n";
            last = m.bucket_count();
        }
    }

    std::cout << "\n== 桶分布：看元素怎么落桶（链地址法）==\n";
    std::unordered_map<int, int> small;
    for (int i = 0; i < 10; ++i) {
        small[i] = i;
    }
    int non_empty = 0;
    for (std::size_t b = 0; b < small.bucket_count(); ++b) {
        if (small.bucket_size(b) > 0) {
            ++non_empty;
            std::cout << "bucket " << b << " 有 " << small.bucket_size(b) << " 个元素\n";
        }
    }
    std::cout << "共 " << small.bucket_count() << " 个桶，" << non_empty << " 个非空\n";
    std::cout << "max_load_factor = " << small.max_load_factor()
              << "（默认 1.0，平均每桶元素数超了就 rehash）\n";

    std::cout << "\n== reserve 预撑桶，避免 hot path 里反复 rehash ==\n";
    std::unordered_map<int, int> reserved;
    reserved.reserve(1000); // 内部按 max_load_factor 算出足够的桶
    std::cout << "reserve(1000) 后 bucket_count = " << reserved.bucket_count() << '\n';
    return 0;
}
