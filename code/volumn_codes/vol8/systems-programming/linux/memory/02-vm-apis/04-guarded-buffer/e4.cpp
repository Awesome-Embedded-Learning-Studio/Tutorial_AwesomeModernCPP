// E4(招牌): guarded_buffer<T> —— 用 PROT_NONE guard page 把"无声越界"变成
// "当场 SIGSEGV 且 si_addr 指到越界的那 1 个字节"。
//
// 布局要诀:用户要 N 字节,咱们给 ceil(N/page) 页数据 + 1 页 PROT_NONE,再把数据
// 的末字节对齐到 guard 页的页首 —— 于是 data[N](越界第 1 字节)恰好踩进 guard,
// 越界多深就落进 guard 多深,si_addr 直接报出越界位置。
//
// 流程:构造 -> 正常读写 -> 故意越界 1 字节(handler 打印 si_addr 与 guard 区间对照)
// -> siglongjmp 恢复继续跑 -> 越界整页末字节仍被拦 -> 对照组(无 guard 的同款布局)
// 同样越界却无声通过 -> 下溢不在防护范围(如实演示)。
// handler 只用 write;恢复用 sigsetjmp/siglongjmp(savemask=1)。
#include "sigout.hpp"

#include <sys/mman.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <setjmp.h>
#include <signal.h>

namespace {

constexpr std::size_t kPage = 4096;

sigjmp_buf g_jb;
void* volatile g_guard_lo = nullptr; // guard 页区间,handler 里对照用
void* volatile g_guard_hi = nullptr;
void* volatile g_access = nullptr; // 咱们打算访问的地址(handler 里与 si_addr 对拍)

void write_dec(unsigned long v) {
    char b[24];
    int i = sizeof b - 1;
    b[i--] = '\0';
    if (v == 0) {
        b[i--] = '0';
    }
    for (; v != 0;) {
        b[i--] = static_cast<char>('0' + v % 10);
        v /= 10;
    }
    write_all(b + i + 1);
}

void on_sigsegv(int, siginfo_t* info, void*) {
    write_all("\n[handler] SIGSEGV, si_addr = ");
    write_hex(reinterpret_cast<std::uintptr_t>(info->si_addr));
    const auto addr = reinterpret_cast<std::uintptr_t>(info->si_addr);
    const auto lo = reinterpret_cast<std::uintptr_t>(g_guard_lo);
    const auto hi = reinterpret_cast<std::uintptr_t>(g_guard_hi);
    const auto want = reinterpret_cast<std::uintptr_t>(g_access);
    if (info->si_code == SEGV_ACCERR) {
        write_all(", si_code = 2 (SEGV_ACCERR)");
    }
    write_all("\n[handler] guard = [");
    write_hex(lo);
    write_all(", ");
    write_hex(hi);
    write_all("), si_addr ");
    if (addr >= lo && addr < hi) {
        write_all("IN guard, 越界第 ");
        write_dec(addr - lo + 1);
        write_all(" / 4096 字节");
    } else {
        write_all("NOT in guard (unrelated fault)");
    }
    write_all("; requested address = ");
    write_hex(want);
    write_all(want == addr ? " == si_addr (精确落点)\n" : " != si_addr (!)\n");
    siglongjmp(g_jb, 1); // 回到主流程,进程不死
}

void print_maps_lines(const void* p, std::size_t len) {
    std::FILE* f = std::fopen("/proc/self/maps", "r");
    if (!f) {
        return;
    }
    char line[512];
    const auto lo = reinterpret_cast<std::uintptr_t>(p);
    const auto hi = lo + len;
    while (std::fgets(line, sizeof line, f)) {
        std::uintptr_t s = 0, e = 0;
        if (std::sscanf(line, "%lx-%lx", &s, &e) == 2 && e > lo && s < hi) {
            std::printf("   maps: %s", line);
        }
    }
    std::fclose(f);
}

// ---- 招牌实现 ----
template <class T> class guarded_buffer {
  public:
    explicit guarded_buffer(std::size_t n)
        : n_(n), bytes_(n * sizeof(T)), payload_((bytes_ + kPage - 1) / kPage * kPage) {
        base_ = static_cast<unsigned char*>(::mmap(
            nullptr, payload_ + kPage, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
        if (base_ == MAP_FAILED) {
            std::printf("mmap failed\n");
            std::abort();
        }
        guard_ = base_ + payload_; // 尾页整页降权
        if (::mprotect(guard_, kPage, PROT_NONE) != 0) {
            std::printf("mprotect failed errno=%d\n", errno);
            std::abort();
        }
        data_ = reinterpret_cast<T*>(guard_) - n; // 数据末字节紧贴 guard 页首
    }
    ~guarded_buffer() { ::munmap(base_, payload_ + kPage); }

    guarded_buffer(const guarded_buffer&) = delete;
    guarded_buffer& operator=(const guarded_buffer&) = delete;

    T* data() const noexcept { return data_; }
    std::size_t size() const noexcept { return n_; }
    unsigned char* guard_begin() const noexcept { return guard_; }
    unsigned char* guard_end() const noexcept { return guard_ + kPage; }

  private:
    std::size_t n_;
    std::size_t bytes_;
    std::size_t payload_;
    unsigned char* base_ = nullptr;
    unsigned char* guard_ = nullptr;
    T* data_ = nullptr;
};

} // namespace

int main() {
    std::printf("E4: guarded_buffer<T> -- turn silent overflow into precise SIGSEGV"
                " (page %ld)\n",
                ::sysconf(_SC_PAGE_SIZE));

    struct sigaction sa{};
    sa.sa_sigaction = on_sigsegv;
    sa.sa_flags = SA_SIGINFO;
    ::sigaction(SIGSEGV, &sa, nullptr);

    // ---- 构造 ----
    guarded_buffer<char> gb(100);
    char* d = gb.data();
    g_guard_lo = gb.guard_begin();
    g_guard_hi = gb.guard_end();
    std::printf("\n== construct guarded_buffer<char>(100) ==\n");
    std::printf("   data  = [%p, %p) 100 bytes, 末字节紧贴 guard 页首\n", static_cast<void*>(d),
                static_cast<void*>(d + 100));
    std::printf("   guard = [%p, %p) PROT_NONE\n", gb.guard_begin(), gb.guard_end());
    std::printf("   data+100 == guard_begin ? %s\n",
                static_cast<void*>(d + 100) == static_cast<void*>(gb.guard_begin()) ? "yes" : "NO");
    print_maps_lines(gb.guard_begin() - kPage, 2 * kPage);

    // ---- 正常读写 ----
    std::printf("\n== normal read/write ==\n");
    unsigned checksum = 0;
    for (std::size_t i = 0; i < gb.size(); ++i) {
        d[i] = static_cast<char>('a' + i % 26);
        checksum += static_cast<unsigned char>(d[i]);
    }
    unsigned again = 0;
    for (std::size_t i = 0; i < gb.size(); ++i) {
        again += static_cast<unsigned char>(d[i]);
    }
    std::printf("   wrote 100 bytes, checksum %u == readback %u ? %s\n", checksum, again,
                checksum == again ? "yes" : "NO");
    d[99] = 'Z'; // 最后一个合法字节
    std::printf("   d[99]='Z' ok (last legal byte)\n");

    // ---- 越界 1 字节 ----
    std::printf("\n== overflow by exactly 1 byte: d[100] = 'X' ==\n");
    std::fflush(stdout);
    g_access = d + 100;
    if (sigsetjmp(g_jb, 1) == 0) {
        volatile char* p = d + 100;
        *p = 'X';
        std::printf("   NOT REACHED\n");
    } else {
        std::printf("   caught it, process alive, d[99] still '%c'\n", d[99]);
    }

    // ---- 越界到 guard 末字节 ----
    std::printf("\n== overflow 1 page deep: d[100+4095] ==\n");
    std::fflush(stdout);
    g_access = d + 100 + (kPage - 1);
    if (sigsetjmp(g_jb, 1) == 0) {
        volatile char* p = d + 100 + (kPage - 1);
        *p = 'Y';
        std::printf("   NOT REACHED\n");
    } else {
        std::printf("   caught at guard's last byte\n");
    }

    // ---- 越界读也拦 ----
    std::printf("\n== overflow READ: c = d[100+3] ==\n");
    std::fflush(stdout);
    g_access = d + 103;
    if (sigsetjmp(g_jb, 1) == 0) {
        volatile char c = d[103];
        (void)c;
        std::printf("   NOT REACHED\n");
    } else {
        std::printf("   reads are guarded too\n");
    }

    // ---- 模板实例化:int 版 ----
    std::printf("\n== guarded_buffer<int>(10): data[10] is 4 bytes into guard ==\n");
    guarded_buffer<int> gi(10);
    int* di = gi.data();
    g_guard_lo = gi.guard_begin();
    g_guard_hi = gi.guard_end();
    for (int i = 0; i < 10; ++i) {
        di[i] = i * i;
    }
    std::printf("   di[9]=%d ok\n", di[9]);
    std::fflush(stdout);
    g_access = di + 10;
    if (sigsetjmp(g_jb, 1) == 0) {
        volatile int v = di[10]; // 越界第 1 个 int,首字节即 guard 首字节
        (void)v;
        std::printf("   NOT REACHED\n");
    } else {
        std::printf("   caught: di[10] faults at guard start\n");
    }

    // ---- 下溢:不在本设计防护范围(如实) ----
    std::printf("\n== underflow d[-1]: NOT covered by a rear-only guard ==\n");
    g_access = d - 1;
    if (sigsetjmp(g_jb, 1) == 0) {
        volatile char c = d[-1]; // 落在前面的数据页里,映射着呢
        (void)c;
        std::printf("   no signal -- 前向不设防,这就是本设计的边界(前后双 guard 可补)\n");
    } else {
        std::printf("   unexpected catch\n");
    }

    // ---- 对照组:同款布局,不设 guard ----
    std::printf("\n== control group: same layout WITHOUT guard page ==\n");
    unsigned char* base2 = static_cast<unsigned char*>(
        ::mmap(nullptr, 2 * kPage, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    volatile char* c = reinterpret_cast<char*>(base2 + kPage) - 100; // 同样:末字节贴页界
    std::printf("   ctrl buffer at %p, ctrl[100] lands at %p (page 2, rw-p)\n",
                reinterpret_cast<const volatile void*>(c),
                reinterpret_cast<const volatile void*>(c + 100));
    for (int i = 0; i < 100; ++i) {
        c[i] = static_cast<char>('a' + i % 26);
    }
    std::fflush(stdout);
    c[100] = 'X'; // 越界 1 字节:没有 guard,静默通过
    std::printf("   ctrl[100]='X' -- no signal, value read back: '%c' (silent corruption)\n",
                c[100]);
    c[100 + kPage - 1] = 'Y'; // 整页越界:同样无声
    std::printf("   ctrl[100+4095]='Y' -- still no signal\n");
    ::munmap(base2, 2 * kPage);

    std::printf(
        "\nverdict: guard page 把越界从'静默写脏邻居'变成'必炸 + si_addr 报出越界第几字节',\n"
        "而且 siglongjmp 让进程接着活 —— 这就是分配器隔离带/电子围栏的原理\n");
    return 0;
}
