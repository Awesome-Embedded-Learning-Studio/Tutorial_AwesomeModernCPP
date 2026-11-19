// demo_aba.cpp —— CAS 只认值，不认历史：一次可复现的 ABA，和版本号的修法
// 演示用：槽位数组 slots 是普通数组，只靠时间窗编排读写次序，存在数据竞争
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

using namespace std::chrono_literals;

constexpr int kEmpty = -1;

struct Node { int value; int next; };

// 版本一：head 只存槽位号，CAS 只问“号码对不对”
struct NaiveStack {
  std::atomic<int> head{kEmpty};
  Node slots[3] = {};

  void init() {
    slots[1] = {100, 2};              // 槽 1：值 100，下一个是槽 2
    slots[2] = {200, kEmpty};
    head.store(1);
  }
  int pop_with_window() {             // 出栈：读完 head 故意留一个窗口再 CAS
    int cur = head.load();
    if (cur == kEmpty) return kEmpty;
    int nxt = slots[cur].next;
    std::this_thread::sleep_for(150ms);
    if (!head.compare_exchange_strong(cur, nxt)) return kEmpty;
    return slots[cur].value;
  }
  void push(int idx, int value) {
    slots[idx] = {value, head.load()};
    head.store(idx);
  }
};

// 版本二：head 把版本号和槽位号打包，动一次版本涨一次
struct VersionStack {
  static unsigned long long pack(int idx, unsigned long long ver) {
    return (ver << 8) | (unsigned long long)(unsigned char)idx;
  }
  std::atomic<unsigned long long> head{pack(kEmpty, 0)};
  Node slots[3] = {};

  void init() {
    slots[1] = {100, 2};
    slots[2] = {200, kEmpty};
    head.store(pack(1, 0));
  }
  int pop_with_window() {
    unsigned long long cur = head.load();
    int idx = (int)(cur & 0xFF);
    if (idx == kEmpty) return kEmpty;
    int nxt = slots[idx].next;
    std::this_thread::sleep_for(150ms);
    for (int attempt = 1; ; ++attempt) {
      unsigned long long want = pack(nxt, (cur >> 8) + 1);
      unsigned long long stale = cur;
      if (head.compare_exchange_strong(cur, want))
        return slots[idx].value;
      cur = head.load();
      std::printf("    A 第 %d 次 CAS 失败：手里的 head=%llu，现在已经是 %llu\n",
                  attempt, stale, cur);
      idx = (int)(cur & 0xFF);
      if (idx == kEmpty) return kEmpty;
      nxt = slots[idx].next;
    }
  }
  void push(int idx, int value) {
    unsigned long long cur = head.load();
    slots[idx] = {value, (int)(cur & 0xFF)};
    head.store(pack(idx, (cur >> 8) + 1));
  }
};

std::atomic<bool> a_read{false};

void wait_for_a() {                   // 手动轮询等 A 读完（demo 有意不用 wait/notify）
  while (!a_read.load()) {}
}

void run_naive() {
  NaiveStack s;
  s.init();
  std::thread a([&] {
    int cur = s.head.load();
    int nxt = s.slots[cur].next;
    std::printf("[naive  ] A 读完：head=%d，下一个是槽 %d，随后睡 150 ms\n", cur, nxt);
    a_read.store(true);
    int got = s.pop_with_window();
    std::printf("[naive  ] A 的 CAS(head: 1 -> 2) 成功，它报告 pop 到了 %d\n", got);
  });
  wait_for_a();
  std::this_thread::sleep_for(10ms);
  int first = s.slots[1].value;
  s.head.store(s.slots[1].next);      // main 的第一次 pop：head 1 -> 2
  std::printf("[naive  ] main: pop 收到 %d\n", first);
  int second = s.slots[2].value;
  s.head.store(s.slots[2].next);      // 第二次 pop：head 2 -> 空
  std::printf("[naive  ] main: pop 收到 %d\n", second);
  s.push(1, 300);                     // 复用槽 1，head 指回 1
  std::printf("[naive  ] main: 把 300 放进槽 1，head 指回 1\n");
  a.join();
  int top = s.head.load();
  std::printf("[naive  ] main 清点：head=%d，栈里只剩值 %d。300 不见了，200 已经交过一回货\n",
              top, s.slots[top].value);
}

void run_version() {
  VersionStack s;
  s.init();
  std::thread a([&] {
    unsigned long long cur = s.head.load();
    std::printf("[version] A 读完：head=%llu（版本 %llu，槽 %llu），随后睡 150 ms\n",
                cur, cur >> 8, cur & 0xFF);
    a_read.store(true);
    int got = s.pop_with_window();
    std::printf("[version] A 最终 pop 到 %d\n", got);
  });
  wait_for_a();
  std::this_thread::sleep_for(10ms);
  s.head.store(VersionStack::pack(s.slots[1].next, 1));
  std::printf("[version] main: pop 收到 %d\n", s.slots[1].value);
  s.head.store(VersionStack::pack(s.slots[2].next, 2));
  std::printf("[version] main: pop 收到 %d\n", s.slots[2].value);
  s.push(1, 300);
  std::printf("[version] main: 把 300 放进槽 1，head=%llu（版本 3，槽 1）\n", s.head.load());
  a.join();
  std::printf("[version] main 清点：head=%llu，槽位是空标记，栈空了。300 交给 A，数目一致\n",
              s.head.load());
}

int main() {
  run_naive();
  a_read.store(false);
  run_version();
  return 0;
}
