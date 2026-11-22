// 引用直接传给 std::thread 编译不过:decay-copy 后副本无法绑回非常量引用
// 错误写法留在注释里,放开后报错见正文;正确写法用 std::ref
#include <print>
#include <thread>

void update_value(int& x) {
    x = 42;
}

int main() {
    int value = 0;
    // std::thread t(update_value, value);  // 编译不过
    std::thread t(update_value, std::ref(value));
    t.join();
    std::println("value = {}", value);
}
