// e1e_linux_side.cpp —— Linux 侧对照:open() 两次偏移独立,dup() 共享;lseek 越 EOF 写出洞
//
// 编译(WSL 原生 g++,与 Windows 侧同一台双系统机器):
//   g++ -std=c++20 -Wall -Wextra e1e_linux_side.cpp -o e1e_linux_side
// 运行:
//   ./e1e_linux_side
//
// 观察点(与 e1d/e1b 逐条对齐):
//   [1] open() 两次:fd1 读 4 字节只推 fd1->f_pos,fd2 从头读
//   [2] dup(fd1):fd3 与 fd1 共享同一个打开文件描述(ofd),偏移互相推进
//   [3] lseek 越过 EOF 再写:st_size 跳到 1MiB+1,洞读为零,st_blocks 只算实写
//   [4] fstat 的 st_blksize/st_blocks 与 Windows AllocationSize 的对应关系
//
// 机制:POSIX 打开文件描述(open file description)持有 f_pos;open() 各造一个,
//       dup/fork 复制的是 fd 表项 -> 指向同一个 ofd -> 偏移共享。
//       Windows 的"文件对象"就是 ofd 的对应物,CreateFileW/DuplicateHandle 一一映射。

#include <cstdio>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

int main() {
    const char* path = "/tmp/supp_e1_linux.bin";
    int seed = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    write(seed, "0123456789", 10);
    close(seed);

    // ---------- [1] open x2:偏移独立 ----------
    int fd1 = open(path, O_RDONLY);
    int fd2 = open(path, O_RDONLY);
    char buf[8];
    ssize_t n;
    printf("[1] open x2:fd1=%d fd2=%d\n", fd1, fd2);
    n = read(fd1, buf, 4);
    buf[n] = 0;
    printf("    fd1 读 4 字节:\"%s\"  -> fd1 偏移=%lld fd2 偏移=%lld\n", buf,
           (long long)lseek(fd1, 0, SEEK_CUR), (long long)lseek(fd2, 0, SEEK_CUR));
    n = read(fd2, buf, 4);
    buf[n] = 0;
    printf("    fd2 读 4 字节:\"%s\"  -> fd1 偏移=%lld fd2 偏移=%lld  <- fd2 从 0 读起\n", buf,
           (long long)lseek(fd1, 0, SEEK_CUR), (long long)lseek(fd2, 0, SEEK_CUR));

    // ---------- [2] dup:偏移共享 ----------
    int fd3 = dup(fd1);
    n = read(fd3, buf, 4);
    buf[n] = 0;
    printf("[2] dup(fd1)=fd3 读 4 字节:\"%s\" -> fd1 偏移=%lld fd3 偏移=%lld  <- 互相推进\n", buf,
           (long long)lseek(fd1, 0, SEEK_CUR), (long long)lseek(fd3, 0, SEEK_CUR));
    lseek(fd3, -4, SEEK_CUR);
    printf("    fd3 回退 -4         -> fd1 偏移=%lld fd3 偏移=%lld  <- fd1 跟着回\n",
           (long long)lseek(fd1, 0, SEEK_CUR), (long long)lseek(fd3, 0, SEEK_CUR));

    close(fd1);
    close(fd2);
    close(fd3);

    // ---------- [3][4] lseek 越 EOF 写:洞 ----------
    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    write(fd, "AB", 2);
    lseek(fd, 1 << 20, SEEK_SET);
    write(fd, "Z", 1);
    struct stat st{};
    fstat(fd, &st);
    printf("[3] 头部 \"AB\" + 1MiB 处 \"Z\"\n");
    printf("    st_size       = %lld  (= 1MiB + 1)\n", (long long)st.st_size);
    printf("    st_blocks     = %lld  (512B 块,实写数据才占)\n", (long long)st.st_blocks);
    printf("    st_blksize    = %lld  (ext4 分配粒度参照)\n", (long long)st.st_blksize);
    lseek(fd, 0, SEEK_SET);
    unsigned char back[16];
    read(fd, back, 16);
    printf("[4] 读回前 16 字节: ");
    for (int i = 0; i < 16; i++) {
        printf("%02X ", back[i]);
    }
    printf(" <- \"AB\" + 零洞\n");
    close(fd);
    unlink(path);
    printf("收尾:临时文件已清理\n");
    return 0;
}
