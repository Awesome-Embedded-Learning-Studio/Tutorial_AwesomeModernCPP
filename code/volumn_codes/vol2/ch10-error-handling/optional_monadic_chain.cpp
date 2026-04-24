#include <optional>
#include <string>
#include <iostream>

struct UserProfile {
    std::string name;
    int age;
};

std::optional<UserProfile> fetch_from_cache(int user_id) {
    // 模拟：ID 1 在缓存中
    if (user_id == 1) return UserProfile{"Alice", 30};
    return std::nullopt;
}

std::optional<UserProfile> fetch_from_server(int user_id) {
    // 模拟：ID 1 和 2 在服务器上
    if (user_id == 1 || user_id == 2) return UserProfile{"Bob", 25};
    return std::nullopt;
}

std::optional<int> extract_age(const UserProfile& profile) {
    if (profile.age > 0) return profile.age;
    return std::nullopt;
}

int main() {
    int user_id = 1;

    // C++23 monadic 链
    auto age_next = fetch_from_cache(user_id)
        .or_else([user_id]() { return fetch_from_server(user_id); })
        .and_then(extract_age)
        .transform([](int age) { return age + 1; });

    if (age_next) {
        std::cout << "Next year age: " << *age_next << "\n";
    }
}
