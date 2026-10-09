// E1 附:权限位跨 uid 实证的工具进程
// 用法:
//   e1_perm create <name> <八进制mode>   —— umask(0) 下创建并写入一段文本
//   e1_perm open <name>                  —— 只开不建,打印 uid 与结果(内容/errno)
//   e1_perm unlink <name>
// 配合 WSL2 的 `wsl.exe -u root --` 免密 root 会话使用:/dev/shm 是全发行版共享的 tmpfs
// 编译:g++ -std=c++20 -Wall -Wextra -O2 e1_perm.cpp -o e1_perm
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "用法:e1_perm create <name> <mode> | open <name> | unlink <name>\n");
        return 2;
    }
    const char* cmd = argv[1];
    const char* name = argv[2];
    errno = 0;
    if (std::strcmp(cmd, "create") == 0) {
        if (argc < 4) {
            std::fprintf(stderr, "create 需要 mode\n");
            return 2;
        }
        const int mode = std::strtol(argv[3], nullptr, 8);
        umask(0); // 让请求的 mode 原样生效,排除 umask 干扰
        int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, mode);
        if (fd < 0) {
            std::printf("create %s mode %04o:失败 uid=%u %s\n", name, mode,
                        static_cast<unsigned>(getuid()), std::strerror(errno));
            return 1;
        }
        if (ftruncate(fd, 4096) != 0) {
            perror("ftruncate");
            return 1;
        }
        void* p = mmap(nullptr, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (p == MAP_FAILED) {
            perror("mmap");
            return 1;
        }
        std::snprintf(static_cast<char*>(p), 64, "written-by-uid-%u",
                      static_cast<unsigned>(getuid()));
        std::printf("create %s mode %04o:成功 uid=%u 内容\"%s\"\n", name, mode,
                    static_cast<unsigned>(getuid()), static_cast<char*>(p));
        munmap(p, 4096);
        close(fd);
        return 0;
    }
    if (std::strcmp(cmd, "open") == 0) {
        int fd = shm_open(name, O_RDWR, 0);
        if (fd < 0) {
            std::printf("open %s:失败 uid=%u errno=%d (%s)\n", name,
                        static_cast<unsigned>(getuid()), errno, std::strerror(errno));
            return 1;
        }
        void* p = mmap(nullptr, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (p == MAP_FAILED) {
            perror("mmap");
            return 1;
        }
        std::printf("open %s:成功 uid=%u 读到\"%s\"\n", name, static_cast<unsigned>(getuid()),
                    static_cast<char*>(p));
        munmap(p, 4096);
        close(fd);
        return 0;
    }
    if (std::strcmp(cmd, "unlink") == 0) {
        int rc = shm_unlink(name);
        std::printf("unlink %s:rc=%d uid=%u%s%s\n", name, rc, static_cast<unsigned>(getuid()),
                    rc != 0 ? " " : "", rc != 0 ? std::strerror(errno) : "");
        return rc == 0 ? 0 : 1;
    }
    std::fprintf(stderr, "未知子命令 %s\n", cmd);
    return 2;
}
