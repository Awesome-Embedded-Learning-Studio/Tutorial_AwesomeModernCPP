// decay-copy:传给线程的对象是副本,指针和引用传的是地址本身
#include <cstddef>
#include <print>
#include <thread>

class Tracker {
  public:
    explicit Tracker(int v) : value_(v) {}
    Tracker(const Tracker& other) : value_(other.value_) {
        std::println("  copy ctor on thread {}, copy lives at {}", std::this_thread::get_id(),
                     (const void*)this);
    }
    int value() const { return value_; }

  private:
    int value_;
};

void work(Tracker t) {
    std::println("  worker got its own object at {}, value {}", (const void*)&t, t.value());
}

void read_array(int* data, std::size_t n) {
    std::println("  worker sees an int* holding {} elements", n);
    long long s = 0;
    for (std::size_t i = 0; i < n; ++i)
        s += data[i];
    std::println("  sum {}", s);
}

int main() {
    Tracker original(7);
    std::println("main thread id {}", std::this_thread::get_id());
    std::println("original lives at {}", (const void*)&original);
    std::thread t(work, original);
    t.join();

    int arr[5] = {1, 2, 3, 4, 5};
    std::thread t2(read_array, arr, 5);
    t2.join();
}
