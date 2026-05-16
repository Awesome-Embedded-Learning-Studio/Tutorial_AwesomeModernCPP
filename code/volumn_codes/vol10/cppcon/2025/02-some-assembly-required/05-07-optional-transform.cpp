/*
 * 验证：std::optional 的 and_then 和 transform（C++23）
 *
 * 背景：文章展示了 C++23 引入的 std::optional::and_then 和 transform，
 *       用于链式处理可能没有值的场景。这些特性已在 C++23 标准化，
 *       Beman 的 Optional26 项目在此基础上做进一步扩展。
 *
 * 预期结果：
 *   and_then 正确传递有值的 optional，跳过空值；
 *   transform 对有值的 optional 执行转换；
 *   输出：邮箱(大写): ALICE@EXAMPLE.COM
 *
 * 编译命令：
 *   g++ -std=c++23 -O2 -o /tmp/05-07-optional-transform 05-07-optional-transform.cpp
 *
 * 运行：
 *   /tmp/05-07-optional-transform
 *
 * 参考资料：
 *   - https://en.cppreference.com/w/cpp/utility/optional
 *
 * 编译器：GCC 16.1.1
 */

#include <optional>
#include <string>
#include <iostream>

struct UserInfo {
    std::string email;
};

std::optional<UserInfo> find_user(int user_id) {
    if (user_id == 42) {
        return UserInfo{.email = "alice@example.com"};
    }
    return std::nullopt;
}

std::optional<std::string> extract_email(const UserInfo& user) {
    if (user.email.empty()) {
        return std::nullopt;
    }
    return user.email;
}

int main() {
    int input_id = 42;

    auto result = find_user(input_id)
        .and_then(extract_email);

    auto upper_result = result.transform([](const std::string& email) {
        std::string upper = email;
        for (char& c : upper) c = std::toupper(c);
        return upper;
    });

    if (upper_result) {
        std::cout << "邮箱(大写): " << *upper_result << "\n";
    } else {
        std::cout << "无法获取邮箱\n";
    }

    return 0;
}
