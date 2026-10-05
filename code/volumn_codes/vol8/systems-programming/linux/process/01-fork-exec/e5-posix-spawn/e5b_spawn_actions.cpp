// E5b posix_spawn_file_actions:不动父进程的 fd,重定向长在子进程身上
// 编译:g++ -std=c++20 -Wall -Wextra -O2 -o e5b_spawn_actions e5b_spawn_actions.cpp
// 对比:fork 后 dup2 的老办法要 fork 出来才能动 fd;posix_spawn 的 file_actions
//      是一张"孩子出生后、exec 前替它做的事"清单,父进程一个 fd 都不用改。
// 场景一 addopen:子进程 exec 前 open(out1) 并装到 fd 1 —— stdout 重定向;
// 场景二 adddup2:父进程提前 open(out2),清单里写"把我这个 fd dup2 到子进程的 fd 1";
// 场景三 addclose:子进程里把 fd 0 关掉(exec 后的程序看 stdin 是关闭的)。
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

static const char* kOut1 = "/home/charliechen/lp01_scratch/e5b_out1.txt";
static const char* kOut2 = "/home/charliechen/lp01_scratch/e5b_out2.txt";

static void dump_file(const char* path) {
    FILE* f = std::fopen(path, "r");
    if (!f) {
        std::printf("  (打不开 %s)\n", path);
        return;
    }
    char line[256] = {};
    bool any = false;
    while (std::fgets(line, sizeof line, f)) {
        any = true;
        std::printf("  %s 的内容:%s", path, line);
    }
    if (any && line[0] && line[std::strlen(line) - 1] != '\n')
        std::printf("\n");
    std::fclose(f);
}

int main() {
    // —— 场景一:addopen,fd 1 整个换成输出文件 ——
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    // 在子进程里执行:open(kOut1, O_WRONLY|O_CREAT|O_TRUNC, 0644),再 dup2 到 fd 1
    posix_spawn_file_actions_addopen(&fa, 1, kOut1, O_WRONLY | O_CREAT | O_TRUNC, 0644);

    char* const av1[] = {(char*)"echo", (char*)"场景一:我明明是 echo,输出却进了文件", nullptr};
    pid_t pid = -1;
    int rc = posix_spawn(&pid, "/bin/echo", &fa, nullptr, av1, environ);
    if (rc == 0) {
        int raw = 0;
        waitpid(pid, &raw, 0);
        std::printf("场景一 addopen:子退出码=%d,stdout 被换进了文件:\n", WEXITSTATUS(raw));
        dump_file(kOut1);
    } else {
        std::printf("场景一 spawn 失败:%s\n", std::strerror(rc));
    }
    posix_spawn_file_actions_destroy(&fa);
    std::fflush(stdout);

    // —— 场景二:adddup2,父进程提前开的 fd 移交给子进程当 stdout ——
    int out2 = open(kOut2, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out2 < 0) {
        std::perror("open out2");
        return 1;
    }
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, out2, 1); // 子进程里:dup2(out2, 1)
    posix_spawn_file_actions_addclose(&fa, out2);   // 子进程里用不着原 fd,顺手关掉

    char* const av2[] = {(char*)"echo", (char*)"场景二:输出经父进程开的 fd 落地", nullptr};
    rc = posix_spawn(&pid, "/bin/echo", &fa, nullptr, av2, environ);
    if (rc == 0) {
        int raw = 0;
        waitpid(pid, &raw, 0);
        std::printf("场景二 adddup2:子退出码=%d(父进程的 fd %d 没动,还是开着的):\n",
                    WEXITSTATUS(raw), out2);
        dump_file(kOut2);
    } else {
        std::printf("场景二 spawn 失败:%s\n", std::strerror(rc));
    }
    posix_spawn_file_actions_destroy(&fa);
    close(out2);
    std::fflush(stdout);

    // —— 场景三:addclose(0)——子进程眼里的 fd 清单,对照看最直观 ——
    // 两个坑都替读者踩过了:
    // 1) 不能用 sh -c 包一层:sh 启动时发现 std fd 缺失会自动补开到 /dev/null,自愈掉演示;
    // 2) ls 列目录自己要 open 一个 fd,会用最低空闲号——addclose(0) 之后那个位置被 ls 的
    //    目录句柄复用,看 -l 的链接目标就能认出它。
    char* const av3[] = {(char*)"ls", (char*)"-l", (char*)"/proc/self/fd", nullptr};
    std::printf("场景三 对照一,不做任何动作,子进程 ls 自己的 fd 清单:\n");
    std::fflush(stdout);
    posix_spawn_file_actions_init(&fa);
    rc = posix_spawn(&pid, "/usr/bin/ls", &fa, nullptr, av3, environ);
    if (rc == 0) {
        int raw = 0;
        waitpid(pid, &raw, 0);
        (void)raw;
    }
    posix_spawn_file_actions_destroy(&fa);

    std::printf("\n场景三 对照二,addclose(0) 之后,同一清单:fd 0 空出来,被 ls 自己的\n");
    std::printf("目录句柄按最低空闲号复用(看 0 -> 的链接目标):\n");
    std::fflush(stdout);
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addclose(&fa, 0);
    rc = posix_spawn(&pid, "/usr/bin/ls", &fa, nullptr, av3, environ);
    if (rc == 0) {
        int raw = 0;
        waitpid(pid, &raw, 0);
        (void)raw;
    }
    posix_spawn_file_actions_destroy(&fa);
    std::fflush(stdout);
    return 0;
}
