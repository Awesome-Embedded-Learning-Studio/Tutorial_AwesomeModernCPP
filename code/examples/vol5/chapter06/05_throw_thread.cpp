#include <cstdio>
#include <stdexcept>
#include <thread>

int main() {
  std::thread t([] { throw std::runtime_error("job failed"); });
  t.join();
  std::printf("never reached\n");
  return 0;
}
