#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

int counter = 0;   // 故意不用 std::atomic，本实验就是要它出错

static void bump_once(bool wide) {
  if (wide) {
    int local = counter;                                   // 读
    std::this_thread::sleep_for(std::chrono::microseconds(50));
    counter = local + 1;                                   // 写
  } else {
    ++counter;                                             // 读写挤在一条指令附近
  }
}

int main(int argc, char** argv) {
  if (argc < 2) { std::printf("usage: %s rounds [wide]\n", argv[0]); return 1; }
  const int rounds = std::atoi(argv[1]);
  const bool wide = argc > 2;
  int bad = 0;
  for (int r = 0; r < rounds; ++r) {
    counter = 0;
    std::thread a([&] { bump_once(wide); });
    std::thread b([&] { bump_once(wide); });
    a.join();
    b.join();
    if (counter != 2) ++bad;
  }
  std::printf("%s: %d of %d rounds lost an increment\n",
              wide ? "wide" : "tight", bad, rounds);
  return 0;
}
