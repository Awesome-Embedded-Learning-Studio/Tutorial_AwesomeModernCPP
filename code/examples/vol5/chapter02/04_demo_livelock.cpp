// demo_livelock.cpp —— 都在动，谁也没前进：借 barrier 对齐节拍
#include <barrier>
#include <cstdio>
#include <mutex>
#include <thread>

std::mutex m1, m2;
std::barrier sync(2);

void polite(const char* name, std::mutex& mine, std::mutex& theirs) {
  int giveups = 0;
  for (int round = 1; round <= 3; ++round) {
    std::unique_lock<std::mutex> held(mine);
    sync.arrive_and_wait();              // 两边都拿稳自己那把
    std::unique_lock<std::mutex> other(theirs, std::try_to_lock);
    sync.arrive_and_wait();              // 两边都试过了，谁也没过去
    if (other.owns_lock()) {
      std::printf("%s goes through in round %d\n", name, round);
      return;
    }
    std::printf("%s backs off in round %d\n", name, round);
    ++giveups;
  }
  std::printf("%s gave up %d times, moved no work forward\n", name, giveups);
}

int main() {
  std::thread a(polite, "one", std::ref(m1), std::ref(m2));
  std::thread b(polite, "two", std::ref(m2), std::ref(m1));
  a.join();
  b.join();
  std::puts("both threads finished");
}
