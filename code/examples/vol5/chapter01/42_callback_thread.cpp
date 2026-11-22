// stop_callback 在哪个线程上跑:请求前注册的回调跑在请求方,请求后注册的当场跑
#include <print>
#include <stop_token>
#include <thread>

int main() {
    std::stop_source source;
    std::stop_token token = source.get_token();
    std::println("main is thread {}", std::this_thread::get_id());

    std::println("registering a callback, before request");
    std::stop_callback cb(
        token, [] { std::println("  [callback] runs on thread {}", std::this_thread::get_id()); });
    std::println("registration returned");

    std::println("calling request_stop");
    source.request_stop();
    std::println("request_stop returned");

    std::println("registering another callback after the request");
    std::stop_callback late(
        token, [] { std::println("  [late] runs on thread {}", std::this_thread::get_id()); });
    std::println("late registration returned");
}
