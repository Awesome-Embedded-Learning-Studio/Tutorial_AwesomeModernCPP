// E1: mprotect 深讲 —— 三件事
//   (1) guard page 的 si_addr 字节级精度:L02 只演示了 si_addr == 页首,
//       这次故意打在 guard 页的不同字节上,看 si_addr 是否逐字节跟随;
//   (2) W^X 切换的 JIT 三步(RW 写码 -> RX 执行 -> 回 RW 改码),
//       外加 Linux 不强制 W^X 的实测(RWX 一把过 + int3 触发 SIGTRAP);
//   (3) 对进程里各代表段(text/rodata/data/heap/stack/vdso/vvar)逐一调
//       mprotect:直觉以为 text 加 W 会吃 EACCES,实测这台 6.18 内核全部
//       放行(MAP_PRIVATE 的加宽走 COW),真正动不得的另有其人;
//       另附一个反直觉坑:.data 页降权后,进程第一次读 errno 会死在
//       ld.so 的 PLT 懒解析手里(GOT 与 .data 同页)。
// 纪律沿用 L02:每个会触发 SIGSEGV/SIGTRAP 的动作都 fork 子进程去干,
// handler 只用 write 打印,父进程 waitpid 收尸解读;父进程对自己地址空间的
// 降权一律"改完立刻恢复",两次 mprotect 之间不进 libc。
#include "article.hpp"
#include "sigout.hpp"

#include <cstring>
#include <sys/wait.h>
#include <unistd.h>

namespace {

constexpr std::size_t kPage = 4096;

unsigned char* g_base = nullptr; // (1):两页匿名区基址
int* g_heap_probe = nullptr;
char* g_stack_probe = nullptr;

volatile sig_atomic_t g_which = -1;
int g_data = 1; // .data 可写段(声明放前面:on_signal 要恢复它的页权)
constexpr const char* kNames[] = {"",
                                  "g1 write guard+0",
                                  "g2 write guard+last",
                                  "g3 read guard+123",
                                  "wx4 exec int3 under RWX",
                                  "seg5 write .data after RW->R",
                                  "seg6 read heap after ->NONE",
                                  "seg7 write stack page after RW->R",
                                  "seg8 cold errno read under RO .data page"};

// 预热 PLT:运行期索引逼真调 strlen@plt,非字面量 write 逼真调 write@plt。
// 降权 .data 页之后,任何"本进程第一次调用的 libc 符号"的懒解析都要写 GOT,
// 而 GOT 与 .data 同页 —— 不预热的话,死因就不是咱们要演示的那个了。
void prewarm_plt() {
    volatile int i = 1;
    const char* s = kNames[i];
    volatile std::size_t sink = __builtin_strlen(s); // 结果喂 volatile,防止纯函数调用被 DCE
    (void)sink;
    const char c = ' ';
    write_all(&c, 1);
}

void write_dec(long v) {
    char b[24];
    int i = sizeof b - 1;
    b[i--] = '\0';
    if (v == 0) {
        b[i--] = '0';
    }
    for (bool neg = v < 0; v != 0 || neg;) {
        const int d = neg ? -(int)(v % 10) : (int)(v % 10);
        b[i--] = static_cast<char>('0' + d);
        v /= 10;
        if (v == 0 && neg) {
            b[i--] = '-';
            neg = false;
        }
    }
    write_all(b + i + 1);
}

void on_signal(int sig, siginfo_t* info, void*) {
    // 进 handler 第一件事:把 .data 页权恢复(mprotect 异步信号安全,且早已解析)。
    // 不然 handler 自己的 _exit/write/strlen 若还有谁没懒解析过,要写 GOT —— 而
    // GOT 与 .data 同页,handler 会当场上演二次 SIGSEGV,被默认动作直接带走
    ::mprotect(reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(&g_data) & ~(kPage - 1)),
               kPage, PROT_READ | PROT_WRITE);
    write_all("\n[handler] ");
    write_all(sig == SIGTRAP ? "SIGTRAP in scenario \"" : "SIGSEGV in scenario \"");
    write_all(kNames[g_which]);
    write_all("\", si_addr = ");
    write_hex(reinterpret_cast<std::uintptr_t>(info->si_addr));
    // SEGV_MAPERR 与 TRAP_BRKPT 同为 1,不能挤一个 switch,按信号分流;数值原样打出
    write_all(", si_code = ");
    write_dec(info->si_code);
    if (sig == SIGTRAP) {
        write_all(info->si_code == SI_KERNEL
                      ? " (SI_KERNEL)  <- 内核送的 trap,不带精确地址(本机 int3 实测)\n"
                      : " (other)\n");
    } else {
        switch (info->si_code) {
            case SEGV_MAPERR:
                write_all(" (SEGV_MAPERR)  <- 地址不在任何映射里\n");
                break;
            case SEGV_ACCERR:
                write_all(" (SEGV_ACCERR)  <- 映射在,权限不许\n");
                break;
            default:
                write_all("\n");
                break;
        }
    }
    ::_exit(70 + g_which);
}

void run_scenario(const char* name, int expect, void (*body)()) {
    std::printf("-- scenario: %s\n", name);
    std::fflush(stdout);
    pid_t pid = ::fork();
    if (pid == 0) {
        body();
        ::_exit(99);
    }
    int st = 0;
    ::waitpid(pid, &st, 0);
    if (WIFEXITED(st)) {
        const int code = WEXITSTATUS(st);
        if (code == expect) {
            std::printf("   OK: child exited %d as expected\n", code);
        } else {
            std::printf("   child exited %d (expected %d)\n", code, expect);
        }
    } else if (WIFSIGNALED(st)) {
        std::printf("   child killed by signal %d (expected exit %d)\n", WTERMSIG(st), expect);
    }
}

// ---------- (1) guard page 字节精度 ----------

volatile unsigned char* v_at(std::size_t off) {
    return g_base + off;
}

void g1() {
    g_which = 1;
    *v_at(kPage) = 'X';
    ::_exit(0);
} // guard 第一字节
void g2() {
    g_which = 2;
    *v_at(2 * kPage - 1) = 'X';
    ::_exit(0);
} // guard 最后一字节
void g3() {
    g_which = 3;
    volatile unsigned char c = *v_at(kPage + 123);
    (void)c;
    ::_exit(0);
}

// 把 [p, p+len) 覆盖到的 /proc/self/maps 行原样打出来:mprotect 之后 VMA 被撕开,
// guard 页在 maps 里就是独立的一段 ---p
void print_maps_lines(const void* p, std::size_t len) {
    std::FILE* f = std::fopen("/proc/self/maps", "r");
    if (!f) {
        std::printf("   (cannot open /proc/self/maps)\n");
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

// ---------- (2) W^X ----------

void wx_body() {
    unsigned char* code = static_cast<unsigned char*>(
        ::mmap(nullptr, kPage, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    if (code == MAP_FAILED) {
        write_all("   mmap failed\n");
        ::_exit(90);
    }
    char msg[128];

    const unsigned char kRet42[] = {0xB8, 0x2A, 0x00, 0x00, 0x00, 0xC3}; // mov eax,42; ret
    std::memcpy(code, kRet42, sizeof kRet42);
    write_all("   step1 [RW ] memcpy mov eax,42; ret\n");
    ::mprotect(code, kPage, PROT_READ | PROT_EXEC);
    const int r42 = reinterpret_cast<int (*)()>(code)();
    int n = std::snprintf(msg, sizeof msg, "   step2 [RX ] mprotect RX, call -> eax = %d\n", r42);
    write_all(msg, static_cast<std::size_t>(n));

    ::mprotect(code, kPage, PROT_READ | PROT_WRITE);                     // 回 RW 改码
    const unsigned char kRet99[] = {0xB8, 0x63, 0x00, 0x00, 0x00, 0xC3}; // mov eax,99; ret
    std::memcpy(code, kRet99, sizeof kRet99);
    ::mprotect(code, kPage, PROT_READ | PROT_EXEC);
    const int r99 = reinterpret_cast<int (*)()>(code)();
    n = std::snprintf(msg, sizeof msg,
                      "   step3 [RW->RX] patch mov eax,99; ret (x86-64 取指缓存硬件自洽,无需显式 "
                      "flush), call -> eax = %d\n",
                      r99);
    write_all(msg, static_cast<std::size_t>(n));

    // Linux 不强制 W^X:RWX 一把过(macOS 此处 EPERM)
    const int rwx = ::mprotect(code, kPage, PROT_READ | PROT_WRITE | PROT_EXEC);
    n = std::snprintf(msg, sizeof msg,
                      "   step4 mprotect(R|W|X) rc=%d  <- W^X 只有 macOS 强制,Linux 放行\n", rwx);
    write_all(msg, static_cast<std::size_t>(n));
    code[0] = 0xCC; // int3:RWX 下边写边执行,连切换都省了
    write_all("   step4' write int3 under RWX, call ->\n");
    reinterpret_cast<void (*)()>(code)(); // -> SIGTRAP
    write_all("   NOT REACHED\n");
    ::_exit(91);
}
void wx4() {
    g_which = 4;
    wx_body();
}

// ---------- (3) 各段动手实测 ----------

constexpr char k_rodata[] = "RODATA-SENTINEL"; // .rodata 只读段

void* pg(void* p) {
    return reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(p) & ~(kPage - 1));
}

const char* prot_str(int prot) {
    static char buf[4];
    buf[0] = (prot & PROT_READ) ? 'r' : '-';
    buf[1] = (prot & PROT_WRITE) ? 'w' : '-';
    buf[2] = (prot & PROT_EXEC) ? 'x' : '-';
    buf[3] = '\0';
    return buf;
}

// errno 只在失败分支读:本进程的第一次 errno 读留给 seg8 的 GOT 懒解析现场
void try_prot(const char* what, void* addr, int prot) {
    const int rc = ::mprotect(pg(addr), kPage, prot);
    const int e = rc == 0 ? 0 : errno;
    std::printf("   %-26s -> %-4s rc=%2d", what, prot_str(prot), rc);
    if (rc == 0) {
        std::printf(" (ok)\n");
    } else {
        std::printf(" errno=%d (%s)\n", e, std::strerror(e));
    }
}

// 父进程里允许做的:改完立刻恢复,两次 mprotect 之间不进 libc
void cycle_prot(const char* what, void* addr, int mid) {
    const int r1 = ::mprotect(pg(addr), kPage, mid);
    const int r2 = ::mprotect(pg(addr), kPage, PROT_READ | PROT_WRITE);
    std::printf("   %-26s -> %-4s rc=%2d, restore rw rc=%2d\n", what, prot_str(mid), r1, r2);
}

__attribute__((noinline)) void text_sentinel() {}

void seg5() // 子进程把 .data 页降 R 再写
{
    g_which = 5;
    prewarm_plt();
    if (::mprotect(pg(&g_data), kPage, PROT_READ) != 0) {
        ::_exit(90);
    }
    write_all("   seg5: child mprotect(.data page, PROT_READ) rc=0\n");
    g_data = 2;
    ::_exit(0);
}
void seg6() // 子进程把 heap 页降 NONE 再读
{
    g_which = 6;
    if (::mprotect(pg(reinterpret_cast<void*>(g_heap_probe)), kPage, PROT_NONE) != 0) {
        ::_exit(90);
    }
    write_all("   seg6: child mprotect(heap page, PROT_NONE) rc=0\n");
    volatile int c = *g_heap_probe;
    (void)c;
    ::_exit(0);
}
void seg7() // 子进程把(旧帧所在的)栈页降 R 再写:handler 在 altstack 上跑
{
    g_which = 7;
    if (::mprotect(pg(reinterpret_cast<void*>(g_stack_probe)), kPage, PROT_READ) != 0) {
        ::_exit(90);
    }
    write_all("   seg7: child mprotect(stack page, PROT_READ) rc=0\n");
    *g_stack_probe = 7;
    ::_exit(0);
}
void seg8() // .data 页只读状态下做"进程第一次读 errno":PLT 懒解析要写 GOT,而
{           // GOT 与 .data 同页,ld.so 先于我们 SIGSEGV;handler 会先把页权改回来再开口
    g_which = 8;
    prewarm_plt(); // write/strlen 已解析,让死因锁定在 errno 的懒解析上
    if (::mprotect(pg(&g_data), kPage, PROT_READ) != 0) {
        ::_exit(90);
    }
    write_all("   seg8: child mprotect(.data page, PROT_READ) rc=0, now first cold errno read\n");
    errno = 0; // <- 懒解析现场
    write_all("   seg8: errno read ok (PLT was already resolved?)\n");
    ::_exit(0);
}

} // namespace

int main() {
    std::printf("E1: mprotect deep dive (page size %ld)\n", ::sysconf(_SC_PAGE_SIZE));

    // ============ (1) guard page 与 si_addr 精度 ============
    std::printf("\n== (1) guard page: si_addr follows the exact byte ==\n");
    g_base = static_cast<unsigned char*>(
        ::mmap(nullptr, 2 * kPage, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    if (g_base == MAP_FAILED) {
        std::printf("mmap failed\n");
        return 1;
    }
    ::mprotect(g_base + kPage, kPage, PROT_NONE);
    std::printf("data  page = [%p, %p)  rw-p\n", static_cast<void*>(g_base),
                static_cast<void*>(g_base + kPage));
    std::printf("guard page = [%p, %p)  ---p (PROT_NONE)\n", static_cast<void*>(g_base + kPage),
                static_cast<void*>(g_base + 2 * kPage));
    print_maps_lines(g_base, 2 * kPage);

    struct sigaction sa{};
    sa.sa_sigaction = on_signal;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK; // seg7 的栈页被降级,handler 必须有别的栈
    ::sigaction(SIGSEGV, &sa, nullptr);
    ::sigaction(SIGTRAP, &sa, nullptr);
    static char altstack[64 * 1024];
    stack_t ss{};
    ss.ss_sp = altstack;
    ss.ss_size = sizeof altstack;
    ::sigaltstack(&ss, nullptr);

    g_base[kPage - 1] = 'K'; // 数据页最后一字节:合法
    std::printf("data[kPage-1] = 'K' ok (last legal byte)\n");
    run_scenario("g1 write guard+0     -> SIGSEGV, si_addr == guard+0", 71, g1);
    run_scenario("g2 write guard+4095  -> SIGSEGV, si_addr == guard+4095", 72, g2);
    run_scenario("g3 read  guard+123   -> SIGSEGV, si_addr == guard+123", 73, g3);

    // ============ (2) W^X ============
    std::printf("\n== (2) W^X: RW write code -> RX exec -> RW patch -> RX again ==\n");
    run_scenario("wx4 RWX page, write int3, call -> SIGTRAP", 74, wx4);

    // ============ (3) 对各段动手 ============
    std::printf("\n== (3) mprotect on every kind of segment ==\n");
    void* heap = ::malloc(2 * kPage);
    g_heap_probe = static_cast<int*>(heap);
    volatile char stack_local = 0;
    g_stack_probe = reinterpret_cast<char*>(reinterpret_cast<std::uintptr_t>(&stack_local) +
                                            2 * kPage); // 旧帧区,已映射,非活跃页

    // seg8 必须趁"本进程还没读过 errno"时做,放在一切会读 errno 的尝试之前
    std::printf("pitfall probe first (needs cold PLT):\n");
    run_scenario("seg8 cold errno read under RO .data -> SIGSEGV inside ld.so (GOT lazy binding)",
                 78, seg8);
    std::printf("   (&g_data = %p, si_addr 与它同页:那里住着 .got.plt)\n",
                static_cast<void*>(&g_data));

    // text/rodata:来自可执行文件的 MAP_PRIVATE 映射,加 W 的下场
    try_prot("text (function page) r-x", reinterpret_cast<void*>(&text_sentinel),
             PROT_READ | PROT_EXEC);
    try_prot("text (function page) +W ", reinterpret_cast<void*>(&text_sentinel),
             PROT_READ | PROT_WRITE | PROT_EXEC);
    if (::mprotect(pg(reinterpret_cast<void*>(&text_sentinel)), kPage,
                   PROT_READ | PROT_WRITE | PROT_EXEC) == 0) {
        std::printf("   text page really became rwx -- maps says:\n");
        print_maps_lines(pg(reinterpret_cast<void*>(&text_sentinel)), 1);
    }
    ::mprotect(pg(reinterpret_cast<void*>(&text_sentinel)), kPage, PROT_READ | PROT_EXEC);

    try_prot("rodata page           +W ", const_cast<char*>(k_rodata), PROT_READ | PROT_WRITE);
    if (::mprotect(pg(const_cast<char*>(k_rodata)), kPage, PROT_READ | PROT_WRITE) == 0) {
        // 真写一发:MAP_PRIVATE 的加宽走 COW,写的是自己的副本。
        // 访问走 volatile:constexpr 数组经 const_cast 的写回读,编译器会常量折叠掉
        volatile char* p = const_cast<char*>(k_rodata);
        const char before = p[0];
        p[0] = 'X';
        std::printf("   rodata real write under rw: '%c' -> '%c' (COW copy, no signal)\n", before,
                    p[0]);
        p[0] = before;
    }
    ::mprotect(pg(const_cast<char*>(k_rodata)), kPage, PROT_READ);

    // 私有可写区:改完立刻恢复,窗口里不进 libc
    cycle_prot(".data page           ->r ", &g_data, PROT_READ);
    cycle_prot(".data page           rw  ", &g_data, PROT_READ | PROT_WRITE);
    cycle_prot("heap(brk) page       ->---", heap, PROT_NONE);
    try_prot("heap(brk) page        +x  ", heap, PROT_READ | PROT_EXEC);
    cycle_prot("heap(brk) page       rw  ", heap, PROT_READ | PROT_WRITE);

    // 内核替进程准备的特殊映射:vdso / vvar / vsyscall
    std::FILE* f = std::fopen("/proc/self/maps", "r");
    if (f) {
        char line[512];
        bool saw_vsyscall = false;
        while (std::fgets(line, sizeof line, f)) {
            for (const char* nm : {"[vdso]", "[vvar]", "[vsyscall]"}) {
                if (std::strstr(line, nm) == nullptr) {
                    continue;
                }
                std::uintptr_t s = 0;
                std::sscanf(line, "%lx-", &s);
                saw_vsyscall |= std::strcmp(nm, "[vsyscall]") == 0;
                const int rc =
                    ::mprotect(reinterpret_cast<void*>(s), kPage, PROT_READ | PROT_WRITE);
                const int e = rc == 0 ? 0 : errno;
                std::printf("   %-26s -> rw-  rc=%2d", nm, rc);
                if (rc == 0) {
                    std::printf(" (ok!)\n");
                } else {
                    std::printf(" errno=%d (%s)\n", e, std::strerror(e));
                }
            }
        }
        if (!saw_vsyscall) {
            std::printf("   [vsyscall]                 -> 本机 maps 里没有这段映射\n");
        }
        std::fclose(f);
    }

    // 越权访问的三个现场:mprotect 的 rc=0 只是"改成了",真访问才见分晓
    run_scenario("seg5 write .data after RW->R -> SIGSEGV ACCERR", 75, seg5);
    run_scenario("seg6 read heap after ->NONE  -> SIGSEGV ACCERR", 76, seg6);
    run_scenario("seg7 write stack page after RW->R -> SIGSEGV ACCERR (handler on altstack)", 77,
                 seg7);

    std::printf("after: g_data=%d (seg5 never landed), *heap=%d, stack local=%d, rodata=%s\n",
                g_data, *g_heap_probe, static_cast<int>(stack_local), k_rodata);
    ::free(heap);

    std::printf("\ndone\n");
    return 0;
}
