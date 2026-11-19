// probe_broken_promise.cpp —— promise 没给值就销毁，get 收到 broken_promise
#include <cstdio>
#include <future>

int main() {
  std::future<int> f;
  {
    std::promise<int> p;
    f = p.get_future();
  }   // promise 没给值就销毁，共享状态被弃
  try {
    f.get();
  } catch (const std::future_error& e) {
    std::printf("get 抛了 future_error：%s（broken_promise？%s）\n", e.what(),
                e.code() == std::future_errc::broken_promise ? "是" : "否");
  }
  return 0;
}
