// E1d fork 继承的 fd:共享的是"打开文件描述",偏移量只有一份
// 编译:g++ -std=c++20 -Wall -Wextra -O2 -o e1d_shared_offset e1d_shared_offset.cpp
// 与 L01 的分工:L01 讲 dup 之后两个 fd 指向同一份打开文件描述;本实验是 fork 的对照——
//              子进程继承整张 fd 表,表里每一项背后还是原来那份打开文件描述。
//              所以父读一段、子接着读,偏移在两边接力推进;子若自己 open 一份,就各玩各的。
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

static const char* kPath = "/home/charliechen/lp01_scratch/e1d_data.bin";

int main() {
    // 造 64 字节数据,内容是 ASCII 48..111,保证每 16 字节长得不一样
    unsigned char data[64];
    for (int i = 0; i < 64; ++i)
        data[i] = (unsigned char)(48 + i);
    int wfd = open(kPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (wfd < 0) {
        std::perror("open 写");
        return 1;
    }
    if (write(wfd, data, 64) != 64) {
        std::perror("write");
        return 1;
    }
    close(wfd);

    int fd = open(kPath, O_RDONLY);
    if (fd < 0) {
        std::perror("open 读");
        return 1;
    }

    int p2c[2], c2p[2];
    if (pipe(p2c) || pipe(c2p)) {
        std::perror("pipe");
        return 1;
    }

    std::printf("[父] 打开 fd=%d —— 这一个 fd 背后是一份打开文件描述,偏移量只有一份\n", fd);
    std::fflush(stdout);

    pid_t child = fork();
    if (child == 0) {
        close(p2c[1]);
        close(c2p[0]);
        char b;
        if (read(p2c[0], &b, 1) != 1)
            _exit(3); // 等父进程读完头一段

        char buf[17] = {};
        if (read(fd, buf, 16) != 16)
            _exit(4);
        std::printf("[子] 用继承的 fd=%d 读 16 字节:%s 偏移=%lld(接着父的 16 往下读)\n", fd, buf,
                    (long long)lseek(fd, 0, SEEK_CUR));

        int own = open(kPath, O_RDONLY); // 对照:自己另开一份
        char buf2[17] = {};
        if (read(own, buf2, 16) != 16)
            _exit(5);
        std::printf("[子] 自己另 open 的 fd=%d 读 16 字节:%s 偏移=%lld(从 0 重新开始)\n", own, buf2,
                    (long long)lseek(own, 0, SEEK_CUR));
        std::fflush(stdout);
        if (write(c2p[1], "k", 1) != 1)
            _exit(6);
        _exit(0);
    }

    close(p2c[0]);
    close(c2p[1]);
    char buf[17] = {};
    if (read(fd, buf, 16) != 16) {
        std::perror("父读1");
        return 1;
    }
    std::printf("[父] 先读 16 字节:%s 偏移=%lld\n", buf, (long long)lseek(fd, 0, SEEK_CUR));
    std::fflush(stdout);
    if (write(p2c[1], "g", 1) != 1) {
        std::perror("write");
        return 1;
    }
    close(p2c[1]);
    char b;
    if (read(c2p[0], &b, 1) != 1) {
        std::perror("read");
        return 1;
    }
    close(c2p[0]);

    char buf2[17] = {};
    if (read(fd, buf2, 16) != 16) {
        std::perror("父读2");
        return 1;
    }
    std::printf("[父] 子读完后我再读 16 字节:%s 偏移=%lld(子进程的读把我的偏移也推走了)\n", buf2,
                (long long)lseek(fd, 0, SEEK_CUR));

    int st = 0;
    waitpid(child, &st, 0);
    std::printf("结论:fork 复制的是 fd 表,但表里每项指向的打开文件描述仍只有一份,\n");
    std::printf("      父子在同一个偏移上接力读——与 dup 共享偏移是同一件事,fork 是整表继承。\n");
    return 0;
}
