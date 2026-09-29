// 演示 stop_token 的状态查询：request_stop 的返回值只 true 一次，晚派生的 token 也能看见请求
#include <iostream>
#include <stop_token>

int main() {
    std::stop_source source; // 默认构造：分配一份停止状态
    std::stop_token token = source.get_token();

    std::cout << "token.stop_requested() = " << token.stop_requested() << "\n";

    const bool first = source.request_stop();  // true：请求由这次调用发出
    const bool second = source.request_stop(); // false：请求早发出去了

    std::cout << "request_stop first/second = " << first << " " << second << "\n";
    std::cout << "token.stop_requested() = " << token.stop_requested() << "\n";

    // 请求发出之后才派生的 token：一落地就看见 1
    std::stop_token late = source.get_token();
    std::cout << "晚派生 token 的 stop_requested() = " << late.stop_requested() << "\n";
    return 0;
}
