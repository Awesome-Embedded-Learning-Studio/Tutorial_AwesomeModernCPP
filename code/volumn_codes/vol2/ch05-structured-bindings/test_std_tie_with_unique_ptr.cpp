// 验证 std::tie 可以处理 unique_ptr（不可拷贝类型）
// 修正文档中的错误断言

#include <iostream>
#include <tuple>
#include <memory>

int main() {
    std::unique_ptr<int> ptr1;
    std::unique_ptr<int> ptr2;

    // std::tie 创建引用，不进行拷贝，因此可以处理 unique_ptr
    std::tie(ptr1, ptr2) = std::make_tuple(std::make_unique<int>(42), std::make_unique<int>(100));

    std::cout << *ptr1 << ", " << *ptr2 << std::endl;

    return 0;
}
