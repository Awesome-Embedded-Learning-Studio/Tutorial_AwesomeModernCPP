// 验证：string_view 不能隐式转换为 std::string
#include <string>
#include <string_view>

void need_string(const std::string& s) {
    (void)s;
}

int main() {
    std::string_view sv = "hello";

    // 下面这行会编译失败：
    // need_string(sv);

    // 必须显式转换：
    need_string(std::string(sv));

    return 0;
}
