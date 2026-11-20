// atomic_tick.cpp —— 自定义类型过了可平凡复制：atomic<Tick> 能构造，只剩四个操作
// （正文里的 Tick 片段补全成可运行版；fetch_add 编不过，探针回话见正文）
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <type_traits>

struct Tick {
  std::uint32_t seq;        // 序号
  std::uint64_t stamp_ns;   // 纳秒时间戳
};

static_assert(std::is_trivially_copyable_v<Tick>);   // 可平凡复制这一关过了

int main() {
  std::atomic<Tick> t{};
  t.store(Tick{1, 100});
  Tick cur = t.load();
  std::printf("load 回来：seq=%u stamp_ns=%llu\n",
              cur.seq, (unsigned long long)cur.stamp_ns);

  Tick old = t.exchange(Tick{2, 200});
  std::printf("exchange 交回旧值：seq=%u，现在 seq=%u\n", old.seq, t.load().seq);

  Tick expected{2, 200};
  bool ok = t.compare_exchange_strong(expected, Tick{3, 300});
  std::printf("值对上时 CAS：ok=%d，现在 seq=%u\n", ok ? 1 : 0, t.load().seq);
  return 0;
}
