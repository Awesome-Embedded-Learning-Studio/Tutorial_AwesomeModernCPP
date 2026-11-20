// demo_three_designs.cpp —— 同一件活三种写法：独立持有、传递结果、共同修改
#include <chrono>
#include <cstdio>
#include <future>
#include <mutex>
#include <thread>
#include <vector>

using clock_type = std::chrono::steady_clock;

constexpr int kThreads = 4;

long long sum_range(const std::vector<int>& data, int lo, int hi) {
  long long s = 0;
  for (int i = lo; i < hi; ++i) s += static_cast<long long>(data[i]) * data[i];
  return s;
}

long long design_a(const std::vector<int>& data) {
  std::vector<long long> parts(kThreads, 0);
  std::vector<std::thread> threads;
  int step = static_cast<int>(data.size()) / kThreads;
  for (int t = 0; t < kThreads; ++t) {
    long long* slot = &parts[t];           // 每条线程只碰自己的槽位
    threads.emplace_back([&data, slot, t, step] {
      *slot = sum_range(data, t * step, (t + 1) * step);
    });
  }
  for (auto& th : threads) th.join();
  long long total = 0;
  for (long long v : parts) total += v;
  return total;
}

long long design_b(const std::vector<int>& data) {
  std::vector<std::promise<long long>> boxes(kThreads);
  std::vector<std::future<long long>> futs;
  for (auto& box : boxes) futs.push_back(box.get_future());
  std::vector<std::thread> threads;
  int step = static_cast<int>(data.size()) / kThreads;
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&data, &boxes, t, step] {
      boxes[t].set_value(sum_range(data, t * step, (t + 1) * step));
    });
  }
  long long total = 0;
  for (auto& f : futs) total += f.get();   // 算完一条，收一条
  for (auto& th : threads) th.join();
  return total;
}

long long locked_times = 0;

long long design_c(const std::vector<int>& data, int block) {
  std::mutex m;
  long long total = 0;
  std::vector<std::thread> threads;
  int step = static_cast<int>(data.size()) / kThreads;
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&, t] {
      int lo = t * step;
      int hi = (t + 1) * step;
      for (int begin = lo; begin < hi; begin += block) {
        int end = begin + block < hi ? begin + block : hi;
        long long part = sum_range(data, begin, end);
        std::lock_guard<std::mutex> lock(m);   // 锁里只有一次加法
        ++locked_times;
        total += part;
      }
    });
  }
  for (auto& th : threads) th.join();
  return total;
}

int main() {
  const int kN = 30000000;
  std::vector<int> data(kN);
  for (int i = 0; i < kN; ++i) data[i] = i % 1000;

  long long total = 0;
  for (int r = 1; r <= 3; ++r) {
    auto ta = clock_type::now();
    long long va = design_a(data);
    long long da = std::chrono::duration_cast<std::chrono::milliseconds>(clock_type::now() - ta).count();

    auto tb = clock_type::now();
    long long vb = design_b(data);
    long long db = std::chrono::duration_cast<std::chrono::milliseconds>(clock_type::now() - tb).count();

    locked_times = 0;
    auto tc = clock_type::now();
    long long vc = design_c(data, 1024);
    long long dc = std::chrono::duration_cast<std::chrono::milliseconds>(clock_type::now() - tc).count();
    long long c_locks = locked_times;

    locked_times = 0;
    auto td = clock_type::now();
    long long vd = design_c(data, 1);
    long long dd = std::chrono::duration_cast<std::chrono::milliseconds>(clock_type::now() - td).count();
    long long d_locks = locked_times;

    total = va;
    if (vb != va || vc != va || vd != va) {   // 四种写法算的得是同一个数
      std::printf("mismatch: %lld %lld %lld %lld\n", va, vb, vc, vd);
      return 1;
    }
    std::printf("round %d: A %lld ms, B %lld ms, C %lld ms, D %lld ms\n",
                r, da, db, dc, dd);
    if (r == 3) {
      std::printf("C locked %lld times, D locked %lld times\n", c_locks, d_locks);
      std::printf("total = %lld\n", total);
    }
  }
  return 0;
}
