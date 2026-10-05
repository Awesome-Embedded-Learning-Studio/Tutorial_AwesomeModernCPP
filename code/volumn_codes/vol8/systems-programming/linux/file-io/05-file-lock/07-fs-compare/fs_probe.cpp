// e7_fs_probe.cpp —— 同一套锁语义探针,跑在不同文件系统上(文章《文件锁》E7)
//
// 用法:./fs_probe <数据文件路径>
// 三组探针 + 一条 /proc/locks 行:
//   P1 flock 互斥时序:A 持锁 300 ms,B 阻塞版多久拿到
//   P2 同进程重新 open 的自冲突(EWOULDBLOCK)
//   P3 fcntl 陷阱:close 无关 fd → 全部记录锁释放
//   P4 持 flock 时 /proc/locks 的行(设备号字段随文件系统变)
// 输出带着路径与 st_dev,方便 ext4/tmpfs 两份输出对照。
#ifndef _GNU_SOURCE
#    define _GNU_SOURCE
#endif

#include "article.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>

#include <sys/file.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

const auto t0 = std::chrono::steady_clock::now();

long ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                 t0)
        .count();
}

// 在 /proc/locks 里找本文件的行(字段 "maj:min:inode",inode 是十进制)
void show_proc_locks(const char* path) {
    struct ::stat st{};
    unique_fd probe{sys_call("open", ::open, path, O_RDWR)};
    sys_call("fstat", ::fstat, probe.get(), &st);
    const std::string want = std::to_string(static_cast<unsigned long>(st.st_ino));
    std::FILE* f = std::fopen("/proc/locks", "r");
    if (!f) {
        return;
    }
    char line[256];
    while (std::fgets(line, sizeof line, f)) {
        // 简单法:行里出现 ":<inode> " 就认(配合字段形态足够稳)
        char pat[64];
        std::snprintf(pat, sizeof pat, ":%s ", want.c_str());
        if (std::strstr(line, pat) != nullptr) {
            std::printf("  /proc/locks: %s", line);
        }
    }
    std::fclose(f);
}

} // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 2) {
        std::fprintf(stderr, "用法:%s <数据文件路径>\n", argv[0]);
        return 2;
    }
    const char* path = argv[1];
    { // 清场
        unique_fd fd{sys_call("open", ::open, path, O_RDWR | O_CREAT | O_TRUNC, 0666)};
    }
    struct ::stat st{};
    {
        unique_fd fd{sys_call("open", ::open, path, O_RDWR)};
        sys_call("fstat", ::fstat, fd.get(), &st);
    }
    std::printf("文件:%s\n", path);
    std::printf(
        "st_dev=%lu:%lu(十进制)= %02lx:%02lx(十六进制),inode=%lu\n",
        static_cast<unsigned long>(major(st.st_dev)), static_cast<unsigned long>(minor(st.st_dev)),
        static_cast<unsigned long>(major(st.st_dev)), static_cast<unsigned long>(minor(st.st_dev)),
        static_cast<unsigned long>(st.st_ino));

    // P1 flock 互斥时序
    std::printf("\n[P1] flock 互斥:A 持锁 300 ms,B 阻塞版接棒\n");
    {
        unique_fd a{sys_call("open", ::open, path, O_RDWR)};
        sys_call("flock", ::flock, a.get(), LOCK_EX);
        const long hold_start = ms();
        pid_t b = ::fork();
        if (b == 0) {
            unique_fd own{sys_call("open", ::open, path, O_RDWR)};
            sys_call("flock", ::flock, own.get(), LOCK_EX);
            std::printf("  B(pid=%d):阻塞 %ld ms 后拿到锁\n", ::getpid(), ms() - hold_start);
            ::_exit(0);
        }
        ::usleep(300 * 1000);
        sys_call("flock", ::flock, a.get(), LOCK_UN);
        int stb = 0;
        ::waitpid(b, &stb, 0);
    }

    // P2 同进程重新 open 自冲突
    std::printf("\n[P2] 同进程重新 open:flock(LOCK_EX|LOCK_NB)\n");
    {
        unique_fd fd1{sys_call("open", ::open, path, O_RDWR)};
        unique_fd fd2{sys_call("open", ::open, path, O_RDWR)};
        sys_call("flock", ::flock, fd1.get(), LOCK_EX);
        int r = ::flock(fd2.get(), LOCK_EX | LOCK_NB);
        std::printf("  fd2 试锁 = %d, errno=%d(%s)\n", r, errno,
                    errno == EWOULDBLOCK ? "EWOULDBLOCK" : "?");
    }

    // P3 fcntl 陷阱:close 无关 fd 全释放
    std::printf("\n[P3] fcntl:close 无关 fd → 记录锁全释放?\n");
    {
        unique_fd fd1{sys_call("open", ::open, path, O_RDWR)};
        struct ::flock f{};
        f.l_type = F_WRLCK;
        f.l_whence = SEEK_SET;
        f.l_start = 0;
        f.l_len = 100;
        sys_call("F_SETLK", ::fcntl, fd1.get(), F_SETLK, &f);
        auto try_from_other = [&] {
            pid_t p = ::fork();
            if (p == 0) {
                unique_fd own{sys_call("open", ::open, path, O_RDWR)};
                struct ::flock g{};
                g.l_type = F_WRLCK;
                g.l_whence = SEEK_SET;
                g.l_start = 0;
                g.l_len = 100;
                int r = ::fcntl(own.get(), F_SETLK, &g);
                std::printf("  竞争者:F_SETLK = %d%s\n", r, r == -1 ? "(被占)" : "(拿到)");
                ::_exit(0);
            }
            int s = 0;
            ::waitpid(p, &s, 0);
        };
        try_from_other();
        {
            unique_fd fd2{sys_call("open", ::open, path, O_RDONLY)};
        } // close(fd2)
        std::printf("  已 close 无关的 fd2\n");
        try_from_other();
    }

    // P4 持锁时 /proc/locks 的行
    std::printf("\n[P4] 持 flock 时 /proc/locks:\n");
    {
        unique_fd fd{sys_call("open", ::open, path, O_RDWR)};
        sys_call("flock", ::flock, fd.get(), LOCK_EX);
        show_proc_locks(path);
    }
    return 0;
}
