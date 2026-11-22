#include <mutex>

class __attribute__((capability("mutex"))) TicketMutex {
 public:
  void lock() __attribute__((acquire_capability())) { m_.lock(); }
  void unlock() __attribute__((release_capability())) { m_.unlock(); }
 private:
  std::mutex m_;
};

class __attribute__((scoped_lockable)) TicketLock {
 public:
  explicit TicketLock(TicketMutex& mu) __attribute__((acquire_capability(mu)))
      : mu_(&mu) { mu_->lock(); }
  ~TicketLock() __attribute__((release_capability())) { mu_->unlock(); }
  TicketLock(const TicketLock&) = delete;
  TicketLock& operator=(const TicketLock&) = delete;
 private:
  TicketMutex* mu_;
};

class Ledger {
 public:
  void add(int n) {
    TicketLock lk(mu_);
    total_ += n;
  }
  int broken_read() {           // 忘了拿锁，等编译器拦
    return total_;
  }
  int peek_locked() __attribute__((requires_capability(mu_))) {
    return total_;
  }
 private:
  TicketMutex mu_;
  int total_ __attribute__((guarded_by(mu_))) = 0;
};

int use_bad() {
  Ledger led;
  led.add(1);
  return led.broken_read() + led.peek_locked();
}

int main() {
  return use_bad();
}
