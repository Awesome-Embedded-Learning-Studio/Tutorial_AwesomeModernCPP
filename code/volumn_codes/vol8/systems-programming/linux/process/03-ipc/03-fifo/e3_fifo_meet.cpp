// E3a 两个不相干进程靠路径名相会:mkfifo 创建命名管道,读端/写端各自被 exec 成
// 独立进程(与 orchestrator 只有父子名分,通信全靠文件系统里的一个名字)。
// 用法:e3_fifo_meet           → orchestrator
//       e3_fifo_meet reader   → 读端子进程(exec 出来的)
//       e3_fifo_meet writer   → 写端子进程(exec 出来的)
// 编译:g++ -std=c++20 -Wall -Wextra -O2 -I ../common e3_fifo_meet.cpp -o e3_fifo_meet
#include "ipc_util.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
const char* kFifo = "/home/charliechen/lp03_scratch/e3_meet.fifo";
}

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IOLBF, 0);

    if (argc >= 2 && std::strcmp(argv[1], "reader") == 0) {
        const auto t0 = std::chrono::steady_clock::now();
        std::printf("[读端 pid=%d] open(O_RDONLY) ……(此刻还没有任何写者)\n", getpid());
        int fd = sys_call("reader open", open, kFifo, O_RDONLY);
        std::printf("[读端 pid=%d] open 返回 fd=%d,干等了 %.1f ms——写者出现了\n", getpid(), fd,
                    ms_since(t0));
        char buf[256];
        ssize_t r;
        long total = 0;
        std::string got;
        while ((r = read(fd, buf, sizeof buf)) > 0) {
            if (got.size() < 100)
                got.append(buf, static_cast<size_t>(std::min<ssize_t>(r, 100 - got.size())));
            total += r;
        }
        close(fd);
        std::printf("[读端 pid=%d] EOF,共 %ld 字节,开头是:「%s」\n", getpid(), total, got.c_str());
        return 0;
    }
    if (argc >= 2 && std::strcmp(argv[1], "writer") == 0) {
        int fd = sys_call("writer open", open, kFifo, O_WRONLY);
        const char msg[] = "hello,我是跟读端毫无亲缘的写者,咱们只认识这条路径名";
        sys_call("write", write, fd, msg, sizeof msg - 1);
        close(fd);
        std::printf("[写端 pid=%d] 写 %zu 字节后关闭,退出\n", getpid(), sizeof msg - 1);
        return 0;
    }

    // ---- orchestrator ----
    unlink(kFifo);
    sys_call("mkfifo", mkfifo, kFifo, 0666);
    struct stat st{};
    sys_call("stat", stat, kFifo, &st);
    std::printf("mkfifo(\"%s\", 0666) 完成,st_mode=%#o → S_ISFIFO=%s(类型位是 p)\n", kFifo,
                st.st_mode, S_ISFIFO(st.st_mode) ? "true" : "false");
    std::FILE* f = popen(("ls -l " + std::string(kFifo)).c_str(), "r");
    char line[256];
    while (fgets(line, sizeof line, f))
        std::printf("  %s", line);
    pclose(f);

    const auto t0 = std::chrono::steady_clock::now();
    pid_t rd = sys_call("fork", fork);
    if (rd == 0) {
        execl(argv[0], "e3_fifo_meet", "reader", (char*)nullptr);
        _exit(127);
    }
    usleep(800 * 1000); // 让读端先在 open 里挂着
    pid_t wr = sys_call("fork", fork);
    if (wr == 0) {
        execl(argv[0], "e3_fifo_meet", "writer", (char*)nullptr);
        _exit(127);
    }
    int status = 0;
    waitpid(rd, &status, 0);
    waitpid(wr, &status, 0);
    std::printf(
        "orchestrator:两个 exec 出来的进程(pid %d / %d)完成了这次相会,总耗时 %.1f ms;unlink 收尾\n",
        rd, wr, ms_since(t0));
    unlink(kFifo);
    return 0;
}
