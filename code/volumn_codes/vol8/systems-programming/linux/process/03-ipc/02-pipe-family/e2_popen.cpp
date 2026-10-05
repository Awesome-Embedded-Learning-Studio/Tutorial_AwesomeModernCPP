// E2a popen/pclose:shell 管道的便利封装。内部就是 fork+dup2+exec,
// 佐证见 e2_popen_strace.txt(strace -f 跟到子进程里的 dup2 与 execve)。
// 编译:g++ -std=c++20 -Wall -Wextra -O2 e2_popen.cpp -o e2_popen
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/wait.h>

int main() {
    // 读方向:拿子进程 stdout
    std::FILE* fp = popen("echo popen-hello-from-shell; uname -r", "r");
    if (!fp) {
        perror("popen");
        return 1;
    }
    char buf[256];
    std::string got;
    while (std::fgets(buf, sizeof buf, fp))
        got += buf;
    int st = pclose(fp);
    std::printf("popen(\"...\") 读到 %zu 字节:\n%s", got.size(), got.c_str());
    std::printf("pclose 返回 %d(子进程 exit %d)\n\n", st, WEXITSTATUS(st));

    // 写方向:喂子进程 stdin
    fp = popen("cat", "w");
    if (!fp) {
        perror("popen");
        return 1;
    }
    std::fputs("popen-写方向:这行经管道进了 cat 的 stdin\n", fp);
    st = pclose(fp);
    std::printf("pclose(写方向) 返回 %d——cat 把上面那行吐回了终端,走的就是我们 dup2 给它的管道\n",
                st);

    // 退出码透传
    fp = popen("exit 7", "r");
    st = pclose(fp);
    std::printf("popen(\"exit 7\") → pclose = %d,WEXITSTATUS = %d:子进程退出状态原样透传\n", st,
                WEXITSTATUS(st));
    return 0;
}
