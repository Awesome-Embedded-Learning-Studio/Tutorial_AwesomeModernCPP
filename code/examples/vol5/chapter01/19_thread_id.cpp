// 线程标识:get_id 的报告与 join 之后的归零
#include <print>
#include <thread>

void worker() {
    std::println("inside worker: my id is {}", std::this_thread::get_id());
}

int main() {
    std::thread t(worker);
    std::println("from main   : worker id is {}", t.get_id());
    std::println("from main   : main id is {}", std::this_thread::get_id());
    t.join();
    std::println("after join  : get_id gives {}", t.get_id());
}
