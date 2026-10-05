// E2c exec 失败不换命:返回 -1,errno 说明原因,原程序继续跑
// 编译:g++ -std=c++20 -Wall -Wextra -O2 -o e2c_exec_fail e2c_exec_fail.cpp
// 三种失败:路径不存在(ENOENT)、文件存在但无执行权限(EACCES)、PATH 里翻不到(ENOENT)。
// 与成功的对照:成功时 exec 一去不回(见 E2a);失败时它就是一个普通函数调用。
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>

static const char* kNoExec = "/home/charliechen/lp01_scratch/e2c_not_executable.sh";

int main() {
    // 造一个存在但没 x 权限的文件
    FILE* f = std::fopen(kNoExec, "w");
    if (f) {
        std::fputs("#!/bin/sh\n", f);
        std::fclose(f);
    }
    chmod(kNoExec, 0644);

    std::printf("活着:pid=%d,开始三次注定失败的 exec\n", getpid());
    std::fflush(stdout);

    char* const av[] = {(char*)"x", nullptr};
    char* const ev[] = {nullptr};

    if (execve("/home/charliechen/lp01_scratch/e2c_missing_path", av, ev) == -1)
        std::printf("1) execve(路径不存在) 返回 -1,errno=%d(%s)\n", errno, std::strerror(errno));

    if (execv(kNoExec, av) == -1)
        std::printf("2) execv(文件在、没 x 权限) 返回 -1,errno=%d(%s)\n", errno,
                    std::strerror(errno));

    if (execlp("e2c_definitely_not_on_path", "x", (char*)nullptr) == -1)
        std::printf("3) execlp(PATH 里翻不到) 返回 -1,errno=%d(%s)\n", errno, std::strerror(errno));

    std::printf("三次失败后我还活着:pid=%d —— exec 只有成功才有去无回,失败就是普通的函数返回\n",
                getpid());
    std::printf("所以 exec 后面跟 perror+exit 不是摆设,是失败路径;只有 exec 返回了,才是失败了\n");
    return 0;
}
