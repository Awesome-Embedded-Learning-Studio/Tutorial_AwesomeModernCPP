// E1: mprotect 基本语义与 guard page(匿名映射)
// 布局:3 页匿名 RW 映射,把中间一页降为 PROT_NONE 当 guard。
// 每个会触发 SIGSEGV 的动作都放进 fork 出的子进程:handler 用 write 打印
// si_addr / si_code 后 _exit(专属码),父进程 waitpid 收尸并解读。
// 期望:guard 页与只读页写 → SEGV_ACCERR;nullptr → SEGV_MAPERR。
#include "article.hpp"
#include "sigout.hpp"

#include <print>
#include <sys/wait.h>

namespace {

// 场景号必须走 volatile sig_atomic_t:普通全局的 store 可能被编译器
// 重排到触发缺页的访问之后(实测 -O2 下真会发生),handler 只保证看见这两类对象。
// 名字表是只读数据,handler 里引用安全。
volatile sig_atomic_t g_which = -1;
constexpr const char* kNames[] = {"", "s1 read data page", "s2 guard page read",
                                  "s3 write after RW->R", "s4 write after R->RW",
                                  "s5 nullptr read"};

void on_sigsegv(int, siginfo_t* info, void*)
{
    write_all("\n[handler] SIGSEGV in scenario \"");
    write_all(kNames[g_which]);
    write_all("\", si_addr = ");
    write_hex(reinterpret_cast<std::uintptr_t>(info->si_addr));
    switch (info->si_code) {
    case SEGV_MAPERR: write_all(", si_code = SEGV_MAPERR(1)  <- 地址不在任何映射里\n"); break;
    case SEGV_ACCERR: write_all(", si_code = SEGV_ACCERR(2)  <- 映射在,权限不许\n"); break;
    default:          write_all(", si_code = ?\n"); break;
    }
    ::_exit(70 + g_which); // 70..74 按场景区分
}

// fork 一个子进程跑 body,父进程 waitpid 并解读退出码。
// expect:预期退出码;ok_byte 场景里,子进程把读到的字节当退出码传回。
void run_scenario(const char* name, int expect, void (*body)())
{
    std::print("-- scenario: {}\n", name);
    std::fflush(stdout);
    pid_t pid = ::fork();
    if (pid == 0) {
        body();               // 不该走到这儿:要么 _exit(handler),要么 body 自己退
        ::_exit(99);
    }
    int st = 0;
    if (::waitpid(pid, &st, 0) == -1) {
        std::print("   waitpid failed: errno={}\n", errno);
        return;
    }
    if (WIFEXITED(st)) {
        const int code = WEXITSTATUS(st);
        if (code == expect) {
            std::print("   OK: child exited {} as expected\n", code);
        } else if (code == expect + 1000) { // 不会用到,占位
            std::print("   OK\n");
        } else {
            std::print("   child exited {} (expected {})\n", code, expect);
        }
    } else if (WIFSIGNALED(st)) {
        std::print("   child killed by signal {} (expected exit {})\n", WTERMSIG(st), expect);
    }
}

unsigned char* g_base = nullptr;
constexpr std::size_t kPage = 4096;

volatile unsigned char* vpage(std::size_t i) { return g_base + i * kPage; }

void s1_read_ok()        { g_which = 1; volatile unsigned char c = *vpage(0); ::_exit(c); }
void s2_guard_read()     { g_which = 2; volatile unsigned char c = *vpage(1); (void)c; ::_exit(0); }
void s3_downgrade_write(){ // 先写成功,再降级,再写 → 应被 handler 接住
    g_which = 3;
    *vpage(2) = 'W';
    volatile unsigned char c = *vpage(2);
    if (c != 'W') { ::_exit(98); }          // 第一阶段:读写都好
    ::mprotect(g_base + 2 * kPage, kPage, PROT_READ); // 降级:RW -> R
    *vpage(2) = 'X';                        // 再写,SIGSEGV 应在这行爆发
    ::_exit(0);
}
void s4_upgrade_write()  {
    g_which = 4;
    ::mprotect(g_base + 2 * kPage, kPage, PROT_READ);              // 先降到 R
    ::mprotect(g_base + 2 * kPage, kPage, PROT_READ | PROT_WRITE); // 再升回 RW
    *vpage(2) = 'U';
    volatile unsigned char c = *vpage(2);
    ::_exit(c);                              // 'U' = 85
}
void s5_null_read()      { g_which = 5;
                           volatile unsigned char c = *static_cast<volatile unsigned char*>(nullptr);
                           (void)c; ::_exit(0); }

} // namespace

int main()
{
    void* p = ::mmap(nullptr, 3 * kPage, PROT_READ | PROT_WRITE,
                     MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (p == MAP_FAILED) { std::print("mmap failed: {}\n", errno); return 1; }
    g_base = static_cast<unsigned char*>(p);
    std::print("anonymous 3 pages at 0x{:x} .. 0x{:x} (page size {})\n",
               reinterpret_cast<std::uintptr_t>(g_base),
               reinterpret_cast<std::uintptr_t>(g_base + 3 * kPage), kPage);

    // 中间页设为 PROT_NONE:guard page
    if (::mprotect(g_base + kPage, kPage, PROT_NONE) != 0) {
        std::print("mprotect(PROT_NONE) failed: {}\n", errno); return 1;
    }
    std::print("mprotect([+{:+}, +{:+}) -> PROT_NONE  (guard page)\n", kPage, 2 * kPage);

    struct sigaction sa {};
    sa.sa_sigaction = on_sigsegv;
    sa.sa_flags = SA_SIGINFO;
    ::sigaction(SIGSEGV, &sa, nullptr);

    g_base[0] = 'Q'; // 数据页先放个已知字节

    run_scenario("s1 read data page 0            -> no signal, byte travels via exit code", 'Q', s1_read_ok);
    run_scenario("s2 read PROT_NONE guard page 1  -> SIGSEGV SEGV_ACCERR", 72, s2_guard_read);
    run_scenario("s3 write page 2, RW->R, rewrite -> SIGSEGV SEGV_ACCERR", 73, s3_downgrade_write);
    run_scenario("s4 R->RW upgrade, write again   -> no signal, byte = 'U'", 'U', s4_upgrade_write);
    run_scenario("s5 dereference nullptr          -> SIGSEGV SEGV_MAPERR", 75, s5_null_read);

    std::print("done: guard/readonly violations -> SEGV_ACCERR; unmapped -> SEGV_MAPERR\n");
    return 0;
}
