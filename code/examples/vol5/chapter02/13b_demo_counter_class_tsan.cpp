// demo_counter_class_tsan.cpp —— 删掉 value() 里的锁，写线程加一百万次，主线程反复读
#include <cstdio>
#include <mutex>
#include <thread>

class Counter {
  public:
    void add(int n) {
        std::lock_guard<std::mutex> lock(m_);
        value_ += n;
    }

    long long value() const { // 这里的锁删掉了：TSan 报的读就在这一行
        return value_;
    }

  private:
    mutable std::mutex m_;
    long long value_ = 0;
};

int main() {
    Counter c;
    std::thread writer([&] {
        for (int i = 0; i < 1000000; ++i)
            c.add(1);
    });

    long long seen = 0;
    long long reads = 0;
    while (seen < 1000000 && reads < 10000000) {
        seen = c.value(); // 不加锁的读，和写线程的 add 并发跑
        ++reads;
        // 编译器屏障：没有它，GCC 会把这次读提出循环（数据竞争下它有权把 value_
        // 当成不变的），实测 seen 就一直是 0；有了它，每次读都真的落到内存
        asm volatile("" : : : "memory");
    }
    writer.join();
    std::printf("%lld reads, last seen %lld, final value %lld\n", reads, seen, c.value());
    return 0;
}
