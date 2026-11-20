// demo_binary.cpp —— binary_semaphore 当信号用：release 一方交货，acquire 一方收
#include <cstdio>
#include <semaphore>
#include <thread>

int main() {
  int payload = 0;
  std::binary_semaphore delivered{0};

  std::thread sender([&] {
    payload = 42;          // release 之前写的值
    delivered.release();   // 交货，计数从 0 到 1
  });
  std::thread receiver([&] {
    delivered.acquire();   // 没货就睡，拿到信号才回
    std::printf("receiver: 读到 payload = %d\n", payload);
  });
  sender.join();
  receiver.join();
  return 0;
}
