// deferred 与 async 两种启动策略:谁在哪个线程上跑、耗时落在哪一步
#include <chrono>
#include <future>
#include <print>
#include <sstream>
#include <thread>

using namespace std::chrono;

std::string id_str() {
    std::ostringstream oss;
    oss << std::this_thread::get_id();
    return oss.str();
}

int main() {
    const std::string main_id = id_str();

    auto t0 = steady_clock::now();
    auto fut = std::async(std::launch::deferred, [] {
        std::this_thread::sleep_for(milliseconds(300));
        return id_str();
    });
    auto t1 = steady_clock::now();
    const std::string who = fut.get();
    auto t2 = steady_clock::now();

    std::println("async() call spent: {} ms", duration_cast<milliseconds>(t1 - t0).count());
    std::println("get()   spent    : {} ms", duration_cast<milliseconds>(t2 - t1).count());
    std::println("deferred task ran on {}",
                 who == main_id ? "the calling thread" : "another thread");

    auto af = std::async(std::launch::async, [] { return id_str(); });
    std::println("std::launch::async task ran on {}",
                 af.get() == main_id ? "the calling thread" : "another thread");
}
