// demo_level_mutex.cpp —— 分层：拿反了当场抛，不进锁里等
#include <climits>
#include <cstdio>
#include <mutex>
#include <stdexcept>
#include <thread>

class LevelMutex {
 public:
  explicit LevelMutex(int level) : level_(level) {}

  void lock() {
    check();
    m_.lock();
    previous_ = current_level_;
    current_level_ = level_;
  }
  void unlock() {
    current_level_ = previous_;
    m_.unlock();
  }
  bool try_lock() {
    check();
    if (!m_.try_lock()) return false;
    previous_ = current_level_;
    current_level_ = level_;
    return true;
  }

 private:
  void check() const {
    if (level_ >= current_level_) {
      throw std::logic_error("lock level goes the wrong way");
    }
  }

  static thread_local int current_level_;
  std::mutex m_;
  int level_;
  int previous_ = 0;
};

thread_local int LevelMutex::current_level_ = INT_MAX;

LevelMutex config(300);   // 越靠上层，编号越大
LevelMutex cache(200);
LevelMutex device(100);

void read_device() {
  std::lock_guard<LevelMutex> g(device);
  std::printf("device: level 100\n");
}
void read_cache() {
  std::lock_guard<LevelMutex> g(cache);
  std::printf("cache: level 200, going down to device\n");
  read_device();
}
void read_all() {
  std::lock_guard<LevelMutex> g(config);
  std::printf("config: level 300, going down to cache\n");
  read_cache();
}
void read_upward() {
  std::lock_guard<LevelMutex> g(device);
  try {
    std::lock_guard<LevelMutex> up(cache);   // 层级不对，进来之前就抛
    std::printf("upward: got cache as well\n");
  } catch (const std::logic_error& e) {
    std::printf("upward: %s\n", e.what());
  }
}

int main() {
  std::thread down(read_all);
  std::thread up(read_upward);
  down.join();
  up.join();
  std::printf("both threads finished\n");
  return 0;
}
