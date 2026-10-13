// e3_proc_locks.cpp —— /proc/locks 观察(文章《文件锁:flock 与 fcntl 记录锁》E3)
//
// 持锁者给同一文件上两样东西:flock 独占锁 + fcntl 写记录锁 [0,100);
// 再派一个被 flock 挡住的阻塞等待者、一个被 fcntl 挡住的 F_SETLKW 等待者。
// 父进程趁锁全部在场时读 /proc/locks,按 inode 过滤出真实行,并逐字段解码。
//
// 行格式(实测内核 6.18.33.2,与 fs/locks.c 的 lock_get_status() 逐字段对上):
//   序号: 类别 咨询性 类型 属主pid 设备号:inode 起始 结束
//   1: FLOCK  ADVISORY  WRITE 1234 08:30:1690235 0 EOF
//   序号自增;类别 FLOCK/POSIX/OFDLCK/LEASE/DELEG;被挡的等待请求以 "->" 缩进挂在其
//   挡路者行后;设备号 maj:min 是 %02x 十六进制,inode 是 %llu 十进制(注意:不是
//   十六进制,拿 stat 的十进制 inode 直接对);flock 行区间恒为 "0 EOF"。
#ifndef _GNU_SOURCE
#    define _GNU_SOURCE
#endif

#include "article.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <sys/file.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

const char* path = "/home/charliechen/l05_scratch/e3/locks.bin";

const auto t0 = std::chrono::steady_clock::now();

long ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                 t0)
        .count();
}

// 按字段精确匹配:找出形如 "maj:min:inode" 的字段,比较最后一段与十进制 inode
bool line_matches(const char* line, const std::string& dec_inode) {
    const char* p = line;
    for (;;) {
        while (*p == ' ' || *p == '\n' || *p == '\t') {
            ++p;
        }
        if (*p == '\0') {
            break;
        }
        const char* tok = p;
        while (*p != '\0' && *p != ' ' && *p != '\n') {
            ++p;
        }
        std::string s{tok, static_cast<std::size_t>(p - tok)};
        auto last = s.rfind(':');
        if (last != std::string::npos && s.substr(last + 1) == dec_inode) {
            return true;
        }
    }
    return false;
}

std::vector<std::string> grep_locks(const std::string& dec_inode) {
    std::vector<std::string> hits;
    std::FILE* f = std::fopen("/proc/locks", "r");
    if (!f) {
        return hits;
    }
    char line[256];
    while (std::fgets(line, sizeof line, f)) {
        if (line_matches(line, dec_inode)) {
            hits.emplace_back(line);
        }
    }
    std::fclose(f);
    return hits;
}

void show(const char* tag, const std::vector<std::string>& lines) {
    std::printf("---- %s ----\n", tag);
    for (const auto& l : lines) {
        std::printf("%s", l.c_str()); // 行自带换行
    }
    if (lines.empty()) {
        std::printf("(没有命中的行)\n");
    }
}

// 把一行按空白切开,给文章当解码样例
void decode(const std::string& line) {
    std::vector<std::string> toks;
    std::string cur;
    for (char c : line) {
        if (c == ' ' || c == '\n') {
            if (!cur.empty()) {
                toks.push_back(cur);
                cur.clear();
            }
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) {
        toks.push_back(cur);
    }
    // "->" 缩进会多占一个字段
    std::size_t off = (!toks.empty() && toks[1] == "->") ? 1 : 0;
    if (toks.size() < off + 6) {
        return;
    }
    std::printf("    解码: 序号%s 类别=%s 性质=%s 类型=%s 属主pid=%s 设备:inode=%s 区间=[%s, %s]\n",
                toks[0].c_str(), toks[off + 1].c_str(), toks[off + 2].c_str(),
                toks[off + 3].c_str(), toks[off + 4].c_str(), toks[off + 5].c_str(),
                toks[off + 6].c_str(), toks[off + 7].c_str());
}

} // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const bool waiter_mode = argc > 1 && std::strcmp(argv[1], "waiter") == 0;
    const bool posix_waiter = argc > 1 && std::strcmp(argv[1], "posix_waiter") == 0;

    if (waiter_mode) { // 被 flock 挡住的等待者
        unique_fd own{sys_call("open", ::open, path, O_RDWR | O_CREAT, 0666)};
        sys_call("flock", ::flock, own.get(), LOCK_EX); // 阻塞到持锁者退场
        std::printf("  [%6ld ms] flock 等待者(pid=%d):拿到锁,退场\n", ms(), ::getpid());
        ::_exit(0);
    }
    if (posix_waiter) { // 被 fcntl 挡住的 F_SETLKW 等待者
        unique_fd own{sys_call("open", ::open, path, O_RDWR | O_CREAT, 0666)};
        struct ::flock f{};
        f.l_type = F_WRLCK;
        f.l_whence = SEEK_SET;
        f.l_start = 50;
        f.l_len = 100; // [50,150) 与持锁者 [0,100) 重叠
        sys_call("F_SETLKW", ::fcntl, own.get(), F_SETLKW, &f);
        std::printf("  [%6ld ms] F_SETLKW 等待者(pid=%d):拿到 [50,150),退场\n", ms(), ::getpid());
        ::_exit(0);
    }

    // 主进程:清场 + 打印 inode 与设备号(拿十进制 inode 直接对 /proc/locks)
    {
        unique_fd fd{sys_call("open", ::open, path, O_RDWR | O_CREAT | O_TRUNC, 0666)};
    }
    unique_fd me{sys_call("open", ::open, path, O_RDWR)};
    struct ::stat st{};
    sys_call("fstat", ::fstat, me.get(), &st);
    const std::string dec_inode = std::to_string(static_cast<unsigned long>(st.st_ino));
    char dev_hex[32];
    std::snprintf(dev_hex, sizeof dev_hex, "%02lx:%02lx",
                  static_cast<unsigned long>(major(st.st_dev)),
                  static_cast<unsigned long>(minor(st.st_dev)));
    std::printf("pid=%d, 文件:%s\n", ::getpid(), path);
    std::printf("fstat: inode=%lu(十进制), 设备 %lu:%lu(十进制) = %s(十六进制)\n\n",
                static_cast<unsigned long>(st.st_ino), static_cast<unsigned long>(major(st.st_dev)),
                static_cast<unsigned long>(minor(st.st_dev)), dev_hex);
    me.reset();

    // 持锁者:flock(EX) + fcntl W [0,100),同一文件两把不同家族的锁并存
    pid_t holder_pid = ::fork();
    if (holder_pid == 0) {
        unique_fd ff{sys_call("open", ::open, path, O_RDWR)};
        sys_call("flock", ::flock, ff.get(), LOCK_EX);
        unique_fd fp{sys_call("open", ::open, path, O_RDWR)};
        struct ::flock f{};
        f.l_type = F_WRLCK;
        f.l_whence = SEEK_SET;
        f.l_start = 0;
        f.l_len = 100;
        sys_call("F_SETLK", ::fcntl, fp.get(), F_SETLK, &f);
        std::printf("  [%6ld ms] 持锁者(pid=%d):flock(LOCK_EX) + F_SETLK W [0,100) 都已上身\n",
                    ms(), ::getpid());
        ::sleep(1); // 持有窗口,主进程趁机观察
        std::printf("  [%6ld ms] 持锁者:退场(两把锁随进程释放)\n", ms());
        ::_exit(0);
    }

    // 两个等待者:一个挡在 flock 上,一个挡在 fcntl 上
    pid_t w1 = ::fork();
    if (w1 == 0) {
        char* av[] = {argv[0], const_cast<char*>("waiter"), nullptr};
        ::execv(argv[0], av);
        ::_exit(127);
    }
    pid_t w2 = ::fork();
    if (w2 == 0) {
        char* av[] = {argv[0], const_cast<char*>("posix_waiter"), nullptr};
        ::execv(argv[0], av);
        ::_exit(127);
    }

    ::usleep(300 * 1000); // 等持锁者与两个等待者就位
    std::printf("[%6ld ms] /proc/locks 中 inode %s 的行:\n", ms(), dec_inode.c_str());
    auto live = grep_locks(dec_inode);
    show("锁全部在场 + 两个等待者被挡", live);
    for (const auto& l : live) {
        decode(l);
    }

    int st1 = 0, st2 = 0, sth = 0;
    ::waitpid(holder_pid, &sth, 0);
    ::waitpid(w1, &st1, 0);
    ::waitpid(w2, &st2, 0);
    std::printf("\n[%6ld ms] 持锁者退场后,两个等待者都拿到了锁(见上)\n", ms());
    ::usleep(50 * 1000);
    show("全部退场后(应为空)", grep_locks(dec_inode));
    return 0;
}
