#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

int main() {
    fs::path p = "/usr/local/bin/gcc";
    if (fs::exists(p)) {
        std::cout << p << " 存在\n";
    } else {
        std::cout << p << " 不存在\n";
    }
    return 0;
}
