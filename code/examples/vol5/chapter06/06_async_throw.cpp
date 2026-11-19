#include <cstdio>
#include <future>
#include <stdexcept>

int main() {
  auto f = std::async(std::launch::async,
                      []() -> int { throw std::runtime_error("task failed"); });
  try {
    f.get();
  } catch (const std::runtime_error& e) {
    std::printf("caught from future: %s\n", e.what());
  }
  return 0;
}
