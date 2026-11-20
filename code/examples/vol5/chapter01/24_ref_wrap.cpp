// std::ref 与 std::cref:让线程真的引用主线程的对象
#include <functional>
#include <print>
#include <string>
#include <thread>

void append_suffix(std::string& str, const std::string& suffix) {
    std::println("  worker touches str at {}", (void*)&str);
    str += suffix;
}

int main() {
    std::string message = "Hello";
    std::string suffix = " World";
    std::println("main's message lives at {}", (void*)&message);
    std::thread t(append_suffix, std::ref(message), std::cref(suffix));
    t.join();
    std::println("message = {}", message);
}
