// 验证：智能指针不支持从 new 表达式 CTAD
// 编译此文件会失败，证明 unique_ptr 和 shared_ptr 不能这样用

#include <memory>

int main() {
    // 以下代码会编译失败：
    // std::unique_ptr up(new int(42));    // 错误：无法推导模板参数
    // std::shared_ptr sp(new int(42));    // 错误：无法推导模板参数

    // 正确的做法：
    auto up1 = std::make_unique<int>(42);  // 推荐
    auto sp1 = std::make_shared<int>(42);  // 推荐

    std::unique_ptr<int> up2(new int(42)); // 显式指定模板参数
    std::shared_ptr<int> sp2(new int(42)); // 显式指定模板参数

    return 0;
}
