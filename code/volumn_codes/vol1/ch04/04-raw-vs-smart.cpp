#include <iostream>
#include <memory>

void raw_version(bool error)
{
    int* data = new int[100];
    data[0] = 42;

    if (error) {
        return;  // 泄漏！忘记 delete[]
    }

    delete[] data;
}

void smart_version(bool error)
{
    auto data = std::make_unique<int[]>(100);
    data[0] = 42;

    if (error) {
        return;  // 不泄漏——析构函数自动调用 delete[]
    }
}

int main()
{
    std::cout << "=== 错误场景 ===\n";
    raw_version(true);    // 泄漏 400 字节
    smart_version(true);  // 安全

    std::cout << "=== 正常场景 ===\n";
    raw_version(false);   // 正常释放
    smart_version(false); // 正常释放
    return 0;
}
