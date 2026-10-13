// E6:memfd_create——匿名内存的 fd 化(Linux 特有,glibc 2.27+)
// 观察:不留名字(不进 /dev/shm)、/proc/self/fd 里带 "/memfd:名字 (deleted)" 标签、
//      同名再建是两个独立实体、close 即消失(没有 unlink 这一步)
// 给 SCM_RIGHTS 传递铺路:fd 就是全部句柄,配 E3 的 fdpass 即可跨进程送达(ch03 细讲)
// 编译:g++ -std=c++20 -Wall -Wextra -O2 e6_memfd.cpp -o e6_memfd
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

constexpr size_t kSize = 4096;

void dump_fd_link(int fd) {
    char path[64], target[256];
    std::snprintf(path, sizeof path, "/proc/self/fd/%d", fd);
    ssize_t n = readlink(path, target, sizeof target - 1);
    if (n < 0) {
        std::printf("  readlink(%s) 失败:%s\n", path, std::strerror(errno));
        return;
    }
    target[n] = '\0';
    std::printf("  readlink(%s) -> %s\n", path, target);
}

void shell(const char* cmd) {
    std::printf("$ %s\n", cmd);
    std::fflush(stdout);
    std::system(cmd);
    std::fflush(stdout);
}

} // namespace

int main() {
    std::printf("== E6 memfd_create:匿名内存的 fd 化 ==\n\n");

    int fd = memfd_create("lm03_memfd", MFD_CLOEXEC);
    if (fd < 0) {
        perror("memfd_create");
        return 1;
    }
    std::printf("[1] memfd_create(\"lm03_memfd\", MFD_CLOEXEC) = fd %d\n", fd);
    if (ftruncate(fd, static_cast<off_t>(kSize)) != 0) {
        perror("ftruncate");
        return 1;
    }
    auto* p = static_cast<char*>(mmap(nullptr, kSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
    if (p == MAP_FAILED) {
        perror("mmap");
        return 1;
    }
    std::strcpy(p, "hello-memfd");
    std::printf("[2] ftruncate 4096 + mmap MAP_SHARED + 写入 \"hello-memfd\" 完成\n");

    dump_fd_link(fd);
    shell("ls -l /dev/shm");
    std::printf("    ↑ /dev/shm 里没有它:memfd 不占名字空间,名字只是 /proc 里的标签\n");

    // 同名再建:名字不是身份,fd 才是
    int fd2 = memfd_create("lm03_memfd", MFD_CLOEXEC);
    if (fd2 < 0) {
        perror("memfd_create 2");
        return 1;
    }
    ftruncate(fd2, static_cast<off_t>(kSize));
    auto* q = static_cast<char*>(mmap(nullptr, kSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd2, 0));
    if (q == MAP_FAILED) {
        perror("mmap q");
        return 1;
    }
    std::strcpy(q, "second-object");
    std::printf("[3] 同名再建一个 memfd(fd=%d):两个实体互不影响——fd1 处是 \"%s\",fd2 处是 \"%s\"\n",
                fd2, p, q);
    dump_fd_link(fd2);

    std::printf("[4] 收尾:close 两个 fd,引用计数归零对象即消失——没有 shm_unlink 这一步;\n");
    std::printf("    想给别的进程用就传 fd 本身(SCM_RIGHTS,见 E3 的 fdpass)——ch03 IPC 篇细讲\n");
    munmap(p, kSize);
    munmap(q, kSize);
    close(fd);
    close(fd2);
    return 0;
}
