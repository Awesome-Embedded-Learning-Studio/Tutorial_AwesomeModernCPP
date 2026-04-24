#include <expected>
#include <string>
#include <iostream>

std::expected<int, std::string> try_cache(int key) {
    return std::unexpected("cache miss for " + std::to_string(key));
}

std::expected<int, std::string> try_database(int key) {
    return key * 100;  // 模拟从数据库获取
}

int main() {
    auto result = try_cache(42)
        .or_else([](const std::string& err) {
            std::cerr << "Cache failed: " << err << ", trying DB\n";
            return try_database(42);
        });

    if (result) {
        std::cout << "Result: " << *result << "\n";
    } else {
        std::cerr << "Error: " << result.error() << "\n";
    }
}
