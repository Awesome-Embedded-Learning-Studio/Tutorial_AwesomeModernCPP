#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

int main() {
    fs::path dir = "/usr/local/bin";

    for (const auto& entry : fs::directory_iterator(dir)) {
        std::cout << entry.path().filename().string();
        if (entry.is_directory()) {
            std::cout << "/";
        }
        std::cout << "\n";
    }
    return 0;
}
