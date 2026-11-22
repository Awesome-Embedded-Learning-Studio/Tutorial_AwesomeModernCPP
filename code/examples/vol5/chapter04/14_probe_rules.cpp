// probe_rules.cpp —— get_future 与 set_value 都只有一回的额度
#include <cstdio>
#include <future>

int main() {
  std::promise<int> p;
  std::future<int> f = p.get_future();
  (void)f;
  try {
    std::future<int> again = p.get_future();   // 第二回
    (void)again;
  } catch (const std::future_error& e) {
    std::printf("第二回 get_future 抛 future_error：%s（future_already_retrieved？%s）\n",
                e.what(),
                e.code() == std::future_errc::future_already_retrieved ? "是" : "否");
  }

  p.set_value(1);
  try {
    p.set_value(2);                            // 第二回
  } catch (const std::future_error& e) {
    std::printf("第二回 set_value 抛 future_error：%s（promise_already_satisfied？%s）\n",
                e.what(),
                e.code() == std::future_errc::promise_already_satisfied ? "是" : "否");
  }
  return 0;
}
