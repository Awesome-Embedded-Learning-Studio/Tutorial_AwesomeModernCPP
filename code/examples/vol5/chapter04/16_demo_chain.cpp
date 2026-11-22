// demo_chain.cpp —— 结果传递链：A 供数据，B 接上算总和，C 收尾打印
#include <cstdio>
#include <future>
#include <numeric>
#include <thread>
#include <vector>

void stage_a(std::promise<std::vector<int>> out) {
  out.set_value(std::vector<int>{3, 1, 4, 1, 5, 9, 2, 6});
}

void stage_b(std::future<std::vector<int>> in, std::promise<int> out) {
  std::vector<int> xs = in.get();   // 一手接上游的 future
  out.set_value(std::accumulate(xs.begin(), xs.end(), 0));   // 一手供下游的 promise
}

void stage_c(std::future<int> in) {
  std::printf("stage C: 收到总和 %d\n", in.get());
}

int main() {
  std::promise<std::vector<int>> ab;
  std::future<std::vector<int>> f_ab = ab.get_future();
  std::promise<int> bc;
  std::future<int> f_bc = bc.get_future();

  std::thread ta(stage_a, std::move(ab));
  std::thread tb(stage_b, std::move(f_ab), std::move(bc));
  std::thread tc(stage_c, std::move(f_bc));
  ta.join();
  tb.join();
  tc.join();
  return 0;
}
