#include <iostream>

int main()
{
    // 堆分配
    int* p1 = new int(42);
    int* p2 = new int[1000]; // 数组也在堆上

    std::cout << "p1 指向的地址: " << p1 << "\n";
    std::cout << "p2 指向的地址: " << p2 << "\n";

    // 必须手动释放
    delete p1;
    delete[] p2;

    return 0;
}
