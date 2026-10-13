// E2a exec 家族五变体:一个进程在链上自我变身五次,pid 全程不变
// 编译:g++ -std=c++20 -Wall -Wextra -O2 -o e2a_exec_variants e2a_exec_variants.cpp
// 链路:execl -> execv -> execve -> execlp -> execvp,每一棒换一种参数形态,
//      变身后靠 argv[1] 的棒号认出自己的位置。
// 五个变体的参数形态(完整表格见本组 README):
//   execl (path, arg0, arg1, ..., NULL)            完整路径,参数逐个列,哨兵 NULL
//   execv (path, char* const argv[])               完整路径,参数打包成数组
//   execve(path, argv, envp)                       系统调用本尊:连环境数组也自己给
//   execlp(name, arg0, arg1, ..., NULL)            只给文件名,去 PATH 里搜,参数逐个列
//   execvp(name, argv)                             只给文件名,去 PATH 里搜,参数数组
// 说明:为了让 execlp/execvp 能搜到本程序,运行前把程序所在目录 prepend 进 PATH
//      (程序自己也会做,但拿 probe 证明过:不 prepend 时 execlp 确实找不到)。
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

static std::string self_path() {
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0) {
        std::perror("readlink");
        std::exit(1);
    }
    buf[n] = '\0';
    return buf;
}

static std::string base_name(const std::string& p) {
    size_t s = p.rfind('/');
    return s == std::string::npos ? p : p.substr(s + 1);
}

int main(int argc, char** argv) {
    const std::string self = self_path();
    const std::string base = base_name(self);
    const char* step = argc > 1 ? argv[1] : "1";

    if (std::strcmp(step, "P") == 0) { // probe 意外成功的分支(正常路径走不到)
        std::printf("  [probe] execlp 直接成功:当前 PATH 已含本目录,probe 没测出东西\n");
        return 0;
    }

    if (std::strcmp(step, "1") == 0) {
        std::printf("起点:pid=%d,程序 %s\n", getpid(), self.c_str());
        std::fflush(stdout);

        // probe:PATH 还没挂上本目录时,execlp 只认 PATH、不认当前目录
        pid_t pb = fork();
        if (pb == 0) {
            execlp(base.c_str(), base.c_str(), "P", (char*)nullptr);
            std::printf("  [probe] execlp(%s) 失败:%s(PATH 不含本目录时它真的搜不到)\n",
                        base.c_str(), std::strerror(errno));
            std::fflush(stdout);
            _exit(99);
        }
        int st = 0;
        waitpid(pb, &st, 0);
        if (WIFEXITED(st) && WEXITSTATUS(st) == 99)
            std::printf("  [probe] 证实 execlp 的搜索范围是 PATH,不含当前目录\n");
        std::fflush(stdout);

        // 把本目录挂到 PATH 最前;出发 pid 塞进环境,终点用它对账
        const char* old = std::getenv("PATH");
        std::string dir = self.substr(0, self.rfind('/'));
        std::string newpath = dir + ":" + (old ? old : "");
        setenv("PATH", newpath.c_str(), 1);
        setenv("E02A_ORIGIN_PID", std::to_string(getpid()).c_str(), 1);

        std::printf("第 1 棒 execl:完整路径+参数逐个列+NULL 哨兵,变身开始\n");
        std::fflush(stdout);
        execl(self.c_str(), base.c_str(), "2", (char*)nullptr);
        std::perror("execl 不该失败"); // 走到这里说明 exec 失败了
        return 1;
    }

    if (std::strcmp(step, "2") == 0) {
        std::printf("第 2 棒到达:经 execl 变身,pid=%d 还是同一个进程\n", getpid());
        std::printf("      交棒 execv:完整路径+参数打包成 char* 数组\n");
        std::fflush(stdout);
        char a0[64], a1[8];
        std::snprintf(a0, sizeof a0, "%s", base.c_str());
        std::snprintf(a1, sizeof a1, "3");
        char* av[] = {a0, a1, nullptr};
        execv(self.c_str(), av);
        std::perror("execv");
        return 1;
    }

    if (std::strcmp(step, "3") == 0) {
        std::printf("第 3 棒到达:经 execv 变身\n");
        std::printf("      交棒 execve:系统调用本尊,路径+argv+环境数组全套自己给\n");
        std::fflush(stdout);
        char a0[64], a1[8];
        std::snprintf(a0, sizeof a0, "%s", base.c_str());
        std::snprintf(a1, sizeof a1, "4");
        char* av[] = {a0, a1, nullptr};
        const char* path_env = std::getenv("PATH");
        const char* origin = std::getenv("E02A_ORIGIN_PID");
        static char e0[64], e1[64], e2[4096];
        std::snprintf(e0, sizeof e0, "E02A_VIA=execve");
        std::snprintf(e1, sizeof e1, "E02A_ORIGIN_PID=%s", origin ? origin : "?");
        std::snprintf(e2, sizeof e2, "PATH=%s", path_env ? path_env : "");
        char* envp[] = {e0, e1, e2, nullptr};
        execve(self.c_str(), av, envp);
        std::perror("execve");
        return 1;
    }

    if (std::strcmp(step, "4") == 0) {
        const char* via = std::getenv("E02A_VIA");
        std::printf("第 4 棒到达:经 execve 变身,自定义 envp 生效:E02A_VIA=%s\n",
                    via ? via : "(没带到?)");
        std::printf("      交棒 execlp:只给文件名,自己去 PATH 里搜\n");
        std::fflush(stdout);
        execlp(base.c_str(), base.c_str(), "5", (char*)nullptr);
        std::perror("execlp");
        return 1;
    }

    if (std::strcmp(step, "5") == 0) {
        std::printf("第 5 棒到达:execlp 在 PATH 里搜到了我\n");
        std::printf("      交棒 execvp:文件名+char* 数组,同样搜 PATH\n");
        std::fflush(stdout);
        char a0[64], a1[8];
        std::snprintf(a0, sizeof a0, "%s", base.c_str());
        std::snprintf(a1, sizeof a1, "6");
        char* av[] = {a0, a1, nullptr};
        execvp(base.c_str(), av);
        std::perror("execvp");
        return 1;
    }

    if (std::strcmp(step, "6") == 0) {
        const char* origin = std::getenv("E02A_ORIGIN_PID");
        char mine[16];
        std::snprintf(mine, sizeof mine, "%d", getpid());
        std::printf("终点:经 execvp 变身。当前 pid=%s,出发时 pid=%s,%s\n", mine,
                    origin ? origin : "?",
                    (origin && std::strcmp(origin, mine) == 0) ? "五次变身 pid 从未变"
                                                               : "pid 竟然变了(不该发生)");
        std::printf(
            "      出发时塞进环境的变量一路带到终点:exec 换的是程序映像,进程还是那个进程\n");
        return 0;
    }

    std::printf("未知棒号:%s\n", step);
    return 1;
}
