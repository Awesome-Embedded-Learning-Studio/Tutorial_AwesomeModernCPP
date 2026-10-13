// E4 孤儿进程与收养:父进程死在孩子前面、孩子还在,谁接管?孤儿为什么不会变僵尸?
// 编译:g++ -std=c++20 -Wall -Wextra -O2 -o e4_orphan e4_orphan.cpp
// 结构:观察者 A fork 出临时父进程 B;B fork 出 C 后立刻退场,C 成孤儿。
//      C 实测自己的 ppid 变成了谁(WSL2 上 PID 1 是什么,读 /proc/1 现场取证),
//      pid 经管道 c2a 报给 A;C 自己退出后,A 盯 /proc/C:直接消失、不以僵尸滞留——
//      收养者(通常是 PID 1)负责收尸,这就是孤儿不变僵尸的机制。
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

static std::string read_small_file(const std::string& p) {
    FILE* f = std::fopen(p.c_str(), "r");
    if (!f)
        return std::string("(打不开:") + std::strerror(errno) + ")";
    std::string out;
    char buf[512];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0)
        out.append(buf, n);
    std::fclose(f);
    while (!out.empty() && (out.back() == '\n' || out.back() == '\0'))
        out.pop_back();
    for (auto& c : out)
        if (c == '\0')
            c = ' ';
    return out;
}

static bool read_state_line(pid_t pid, char* out, size_t cap) {
    char path[64];
    std::snprintf(path, sizeof path, "/proc/%d/status", pid);
    FILE* f = std::fopen(path, "r");
    if (!f)
        return false;
    bool found = false;
    while (std::fgets(out, (int)cap, f))
        if (std::strncmp(out, "State:", 6) == 0) {
            found = true;
            break;
        }
    std::fclose(f);
    return found;
}

int main() {
    std::printf("[A] 观察者 pid=%d\n", getpid());
    std::printf("[A] 本机 PID 1 是什么:/proc/1/comm = %s\n",
                read_small_file("/proc/1/comm").c_str());
    std::printf("[A]               /proc/1/cmdline = %s\n",
                read_small_file("/proc/1/cmdline").c_str());
    std::fflush(stdout);

    // c2b:C 报平安给 B(B 才敢退);c2a:C 把自己的 pid 报给 A
    int c2b[2], c2a[2];
    if (pipe(c2b) || pipe(c2a)) {
        std::perror("pipe");
        return 1;
    }

    pid_t B = fork();
    if (B == 0) {
        pid_t C = fork();
        if (C == 0) {      // 孤儿本人:继承全套端口之后,再关自己不用的
            close(c2b[0]); // 平安信只写不读
            close(c2a[0]); // pid 只写不读
            pid_t orig_ppid = getppid();
            pid_t mine = getpid();
            write(c2a[1], &mine,
                  sizeof mine); // 头一件事把 pid 报给 A(这个端口留着,后面还要报收养者)
            std::printf("[C] 出生:pid=%d ppid=%d(临时父 B)\n", mine, orig_ppid);
            std::fflush(stdout);
            write(c2b[1], "x", 1); // 平安报过,B 可以退了
            close(c2b[1]);
            pid_t new_ppid = orig_ppid;
            for (int i = 0; i < 1000 && new_ppid == orig_ppid; ++i) {
                usleep(10000);
                new_ppid = getppid();
            }
            std::printf("[C] ppid 变成了 %d,收养者的 comm = %s\n", new_ppid,
                        read_small_file("/proc/" + std::to_string(new_ppid) + "/comm").c_str());
            std::fflush(stdout);
            write(c2a[1], &new_ppid, sizeof new_ppid); // 把实测收养者 pid 报给 A
            close(c2a[1]);
            std::printf("[C] 我现在是孤儿,再干 1 秒活然后自己退出——看我会不会变僵尸\n");
            std::fflush(stdout);
            usleep(1000000);
            _exit(0);
        }
        // B:C 出生后它继承了需要的端口;B 只留 c2b 读端等平安信
        close(c2a[0]);
        close(c2a[1]);
        close(c2b[1]);
        char b;
        if (read(c2b[0], &b, 1) == 1) { // 等 C 报平安,保证 C 的出生信息排在前面打印
            close(c2b[0]);
            std::printf("[B] 临时父 pid=%d,C 已出生,我退场——C 从此没爹\n", getpid());
            std::fflush(stdout);
        }
        _exit(0);
    }

    // A:c2a 留读端收 C 的 pid,c2b 两端都不用
    close(c2b[0]);
    close(c2b[1]);
    close(c2a[1]);
    pid_t C_pid = -1;
    if (read(c2a[0], &C_pid, sizeof C_pid) != (ssize_t)sizeof C_pid) {
        std::perror("读 C 的 pid");
        return 1;
    }
    // c2a[0] 故意不关:C 稍后还要经它报收养者 pid;关早了 C 的 write 会吃 SIGPIPE

    int st = 0;
    waitpid(B, &st, 0); // B 是 A 的亲儿子,该 A 收;C 是孙辈,血缘上轮不到 A
    errno = 0;
    pid_t denied = waitpid(C_pid, &st, WNOHANG);
    std::printf("[A] B 已被 A 收尸。A 试着多管闲事去 wait 孙辈 C(pid=%d):返回 %d,errno=%d(%s)\n",
                C_pid, denied, errno,
                errno == ECHILD ? "ECHILD:不是我的孩子,没资格" : std::strerror(errno));
    std::printf("[A] 只能旁观:\n");
    std::fflush(stdout);

    char line[128], last[128] = "";
    for (int i = 0; i < 200; ++i) {
        if (!read_state_line(C_pid, line, sizeof line)) {
            std::printf("[A] t+%.1fs:/proc/%d 消失(ENOENT)——C 退出后没以僵尸滞留,收养者收了尸\n",
                        i * 0.05, C_pid);
            pid_t adopter = -1;
            ssize_t got = read(c2a[0], &adopter, sizeof adopter); // C 实测的收养者 pid
            (void)got;
            std::string who =
                adopter > 0
                    ? std::to_string(adopter) + "(" +
                          read_small_file("/proc/" + std::to_string(adopter) + "/comm") + ")"
                    : std::string("(未知)");
            if (adopter == 1) {
                std::printf(
                    "[A] 机制:C 的 ppid 实测变成 %s,即 PID 1(%s)——教科书口径:过继给 init。\n",
                    who.c_str(), read_small_file("/proc/1/cmdline").c_str());
            } else {
                std::printf("[A] 机制:C 的 ppid 实测变成 %s,不是 PID 1(%s)!\n", who.c_str(),
                            read_small_file("/proc/1/comm").c_str());
                std::printf("    内核把孤儿过继给最近的 subreaper 祖先(prctl "
                            "PR_SET_CHILD_SUBREAPER 标记),\n");
                std::printf("    没有才落到 PID 1;本会话里 WSL 的会话级 /init(Relay)就是那个 "
                            "subreaper。\n");
            }
            std::printf("    收养者负责 wait:它退出的瞬间就被收,所以孤儿没机会变僵尸——\n");
            std::printf("    僵尸的定义是死了没人 wait,而收养者永远会 wait。\n");
            std::fflush(stdout);
            return 0;
        }
        if (std::strncmp(line, last, sizeof line) != 0) {
            std::printf("[A] t+%.1fs:%s", i * 0.05, line);
            std::fflush(stdout);
            std::snprintf(last, sizeof last, "%s", line);
        }
        usleep(50000);
    }
    std::printf("[A] 等了 10s C 还在,异常结束\n");
    return 1;
}
