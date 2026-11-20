// demo_pair.cpp —— promise 供值，future 取值：两端都在咱们手里
#include <cstdio>
#include <future>
#include <thread>
#include <vector>

int count_even(const std::vector<int>& xs) {
  int n = 0;
  for (int x : xs) {
    if (x % 2 == 0) ++n;
  }
  return n;
}

void worker(std::promise<int> out) {
  std::vector<int> xs{7, 2, 9, 4, 6, 1, 8, 3};
  out.set_value(count_even(xs));   // 干完活，把结果供进 promise
}

int main() {
  std::promise<int> p;
  std::future<int> f = p.get_future();   // 供值端移动给线程，future 留在 main
  std::thread t(worker, std::move(p));

  int n = f.get();
  std::printf("main: get 到 %d 个偶数，valid() = %d\n", n, f.valid() ? 1 : 0);
  t.join();

  // 供值端不开线程也能用：当场供、当场取
  std::promise<int> here;
  std::future<int> fh = here.get_future();
  here.set_value(42);
  std::printf("main: 当场供的值 %d，全程没动过一条线程\n", fh.get());
  return 0;
}
