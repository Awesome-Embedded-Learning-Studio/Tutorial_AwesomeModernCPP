// 验证 [[nodiscard]] 的强制检查能力
// 编译: g++ -std=c++17 -Wall -Wextra verify_nodiscard.cpp -o verify_nodiscard
// 注意: 编译时会因为第19行未检查返回值而产生警告
// 运行: ./verify_nodiscard

#include <iostream>

// 定义一个带 [[nodiscard]] 的结果类型
struct [[nodiscard]] ExpectedResult {
    int value;
    bool has_error;

    constexpr bool ok() const noexcept { return !has_error; }
};

// 测试：不检查返回值
ExpectedResult returns_important_value() {
    return {42, false};
}

int main() {
    // 这行代码会触发编译器警告（使用了 [[nodiscard]] 返回值但未检查）
    // warning: ignoring returned value of type 'ExpectedResult', declared with attribute 'nodiscard'
    returns_important_value();

    // 正确用法：必须检查返回值
    auto result = returns_important_value();
    if (result.ok()) {
        std::cout << "Value: " << result.value << "\n";
    }

    return 0;
}
