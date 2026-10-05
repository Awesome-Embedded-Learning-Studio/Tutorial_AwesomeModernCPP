// E1c 进程创建成本:1 GiB 父进程的 fork vs vfork vs posix_spawn
// 编译:g++ -std=c++20 -Wall -Wextra -O2 -o e1c_fork_cost e1c_fork_cost.cpp
// 口径:每项预热 3 轮后再计 30 轮,CLOCK_MONOTONIC,报 min/p50/mean/max(µs)。
//      "仅创建"行 = fork/vfork + 子进程立刻 _exit + 父进程 waitpid;
//      "+exec /bin/true"行额外含 exec + 动态链接 + 程序退出的全部成本。
//      小父对照组在本进程还小时就 fork 出去单独量,结果经管道带回,避免"进程变不小"。
// 注意:WSL2 单 NUMA、无 perf,数据是本机口径的数量级参考,不是标称值。
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <spawn.h>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

extern char** environ;

static double now_us() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return double(ts.tv_sec) * 1e6 + double(ts.tv_nsec) / 1e3;
}

static long vm_rss_kb() {
    FILE* f = std::fopen("/proc/self/status", "r");
    if (!f)
        return -1;
    char line[128];
    long v = -1;
    while (std::fgets(line, sizeof line, f))
        if (std::strncmp(line, "VmRSS:", 6) == 0) {
            v = std::atol(line + 6);
            break;
        }
    std::fclose(f);
    return v;
}

template <typename F> static void bench(FILE* out, const char* name, int n, F&& body) {
    for (int i = 0; i < 3; ++i)
        body(); // 预热
    std::vector<double> xs;
    xs.reserve(n);
    for (int i = 0; i < n; ++i) {
        double t0 = now_us();
        int rc = body();
        double dt = now_us() - t0;
        if (rc != 0) {
            std::fprintf(out, "%s: 第 %d 轮失败 rc=%d\n", name, i, rc);
            return;
        }
        xs.push_back(dt);
    }
    std::sort(xs.begin(), xs.end());
    double sum = 0;
    for (double x : xs)
        sum += x;
    std::fprintf(out, "%-30s min=%9.1f  p50=%9.1f  mean=%9.1f  max=%9.1f  (µs, n=%d)\n", name,
                 xs.front(), xs[n / 2], sum / n, xs.back(), n);
    std::fflush(out);
}

static int fork_create() {
    pid_t p = fork();
    if (p == 0)
        _exit(0);
    if (p < 0)
        return 1;
    return waitpid(p, nullptr, 0) == p ? 0 : 2;
}

static int vfork_create() {
    pid_t p = vfork(); // 子进程借父进程的地址空间,只能 _exit 或 exec,不能碰 stdio
    if (p == 0)
        _exit(0);
    if (p < 0)
        return 1;
    return waitpid(p, nullptr, 0) == p ? 0 : 2;
}

static int fork_exec_true() {
    pid_t p = fork();
    if (p == 0) {
        char* const av[] = {(char*)"true", nullptr};
        execve("/bin/true", av, environ);
        _exit(126);
    }
    if (p < 0)
        return 1;
    int raw = 0;
    if (waitpid(p, &raw, 0) != p)
        return 2;
    return (WIFEXITED(raw) && WEXITSTATUS(raw) == 0) ? 0 : 3;
}

static int spawn_true() {
    pid_t pid = -1;
    char* const av[] = {(char*)"true", nullptr};
    int rc = posix_spawn(&pid, "/bin/true", nullptr, nullptr, av, environ);
    if (rc != 0)
        return rc;
    int raw = 0;
    if (waitpid(pid, &raw, 0) != pid)
        return 2;
    return (WIFEXITED(raw) && WEXITSTATUS(raw) == 0) ? 0 : 3;
}

int main() {
    const int N = 30;

    // —— 对照组:趁本进程还小,fork 一个"小父进程"去量小内存口径 ——
    int rep[2];
    if (pipe(rep)) {
        std::perror("pipe");
        return 1;
    }
    pid_t small = fork();
    if (small == 0) {
        close(rep[0]);
        FILE* out = fdopen(rep[1], "w");
        bench(out, "小父 fork 仅创建", N, fork_create);
        bench(out, "小父 vfork 仅创建", N, vfork_create);
        bench(out, "小父 posix_spawn /bin/true", N, spawn_true);
        std::fflush(out);
        _exit(0);
    }
    close(rep[1]);

    // —— 本进程触碰 1 GiB 匿名内存,变成"大父进程" ——
    const size_t kBytes = 1ull << 30;
    unsigned char* big = new unsigned char[kBytes];
    for (size_t i = 0; i < kBytes; i += 4096)
        big[i] = 1;
    std::printf("大父进程已就位:VmRSS=%ld kB(1 GiB 已逐页触碰)\n", vm_rss_kb());
    std::printf("--- 大父进程(1 GiB)口径 ---\n");
    std::fflush(stdout);
    bench(stdout, "fork 仅创建", N, fork_create);
    bench(stdout, "vfork 仅创建", N, vfork_create);
    bench(stdout, "fork+exec /bin/true", N, fork_exec_true);
    bench(stdout, "posix_spawn /bin/true", N, spawn_true);

    // —— 带回小父对照组的数据 ——
    int st = 0;
    waitpid(small, &st, 0);
    FILE* in = fdopen(rep[0], "r");
    char line[256];
    std::printf("--- 小父进程(对照组,VmRSS≈几 MB)口径 ---\n");
    while (in && std::fgets(line, sizeof line, in))
        std::printf("%s", line);
    if (in)
        std::fclose(in);
    std::printf("解读:fork 的成本随父进程已触碰内存线性涨(复制的是页表+RSS 记账,不是页本身);\n");
    std::printf("      vfork/posix_spawn 不复制页表,对 1 GiB 父进程依然只有几十 µs。\n");
    return 0;
}
