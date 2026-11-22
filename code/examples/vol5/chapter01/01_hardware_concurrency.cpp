// 查询机器的并发能力:hardware_concurrency 返回实现支持的并发线程数
#include <print>
#include <thread>

int main() {
    std::println("{}", std::thread::hardware_concurrency());
}
