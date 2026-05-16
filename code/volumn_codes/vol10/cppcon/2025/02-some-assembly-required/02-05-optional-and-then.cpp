#include <optional>
#include <string>
#include <iostream>
#include <cctype>

struct UserInfo { std::string email; };

std::optional<UserInfo> find_user(int user_id) {
    if (user_id == 42) return UserInfo{.email = "alice@example.com"};
    return std::nullopt;
}

std::optional<std::string> extract_email(const UserInfo& user) {
    if (user.email.empty()) return std::nullopt;
    return user.email;
}

int main() {
    // C++23: and_then + transform 链式调用
    auto result = find_user(42)
        .and_then(extract_email)
        .transform([](const std::string& email) {
            std::string upper = email;
            for (char& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            return upper;
        });

    if (result) std::cout << "email: " << *result << "\n";
    else std::cout << "no email\n";

    auto result2 = find_user(99)
        .and_then(extract_email);
    if (result2) std::cout << "email2: " << *result2 << "\n";
    else std::cout << "no email2 (user not found)\n";

    return 0;
}
