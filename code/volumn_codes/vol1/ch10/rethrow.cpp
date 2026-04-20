#include <cstdio>
#include <iostream>
#include <stdexcept>

void wrapper()
{
    try {
        throw std::runtime_error("Runtime failure");
    }
    catch (const std::exception& e) {
        std::fprintf(stderr, "[wrapper] Logging: %s\n", e.what());
        throw;  // 重新抛出原始异常，保持完整类型信息
    }
}

int main()
{
    try {
        wrapper();
    }
    catch (const std::runtime_error& e) {
        std::cout << "Caught: " << e.what() << "\n";
    }
    catch (...) {
        // 捕获所有其他类型的异常
        std::cout << "Caught unknown exception\n";
    }
    return 0;
}
