// 验证：从 const char* 构造 string_view 需要 strlen，是 O(n) 操作
#include <string_view>
#include <cstring>
#include <iostream>

int main() {
    const char* long_str = "This is a very long string that will cause strlen to traverse the entire buffer to find the null terminator";
    std::string_view sv(long_str);  // 内部调用 strlen

    std::cout << "验证：从 const char* 构造 string_view 需要 strlen，是 O(n) 操作\n";
    std::cout << "string_view(const char*) 构造函数内部会调用 strlen 来获取长度\n";
    return 0;
}
