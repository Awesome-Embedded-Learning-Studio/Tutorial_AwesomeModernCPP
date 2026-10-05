// e4_bench.cpp —— E4:10000 文件大目录,四种遍历姿势计时(CLOCK_MONOTONIC,3 轮取中位)
//
//   a) 手搓 opendir+readdir            —— 只拿名字(POSIX 底线)
//   b) fs::directory_iterator          —— 只拿名字
//   c) fs::directory_iterator + entry.is_regular_file()   —— 用 readdir 带出的类型缓存?
//   d) fs::directory_iterator + entry.file_size(ec)       —— 每条现查大小?
// syscalls 谁多谁少,由外层 strace 数(e4_strace_*.out):
//   strace -c -e trace=openat,getdents64,statx,newfstatat,close ./e4_bench one a
// 用法:
//   ./e4_bench setup     # 建 10000 个文件
//   ./e4_bench one a|b|c|d   # 单跑一 variant 一遍(strace 用)
//   ./e4_bench           # 计时主实验:预热 + 每 variant 3 轮
// 编译:g++ -std=c++20 -Wall -Wextra -O2 -I ../common e4_bench.cpp -o e4_bench
#include "fsio.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <time.h>
#include <vector>

namespace fs = std::filesystem;

static const char* kBig = "/home/charliechen/l04_scratch/e4_big";
static const int kFiles = 10000;

static long long now_ns() {
    timespec ts;
    ::clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static void setup() {
    fs::remove_all(kBig);
    fs::create_directories(kBig);
    char name[32];
    for (int i = 0; i < kFiles; ++i) {
        std::snprintf(name, sizeof name, "f%05d", i);
        if (FILE* f = std::fopen((fs::path(kBig) / name).c_str(), "w"))
            std::fclose(f);
    }
    std::printf("setup: %d 个空文件在 %s\n", kFiles, kBig);
}

// 返回「干了的活」计数,sink 汇总防优化
static unsigned long long run(char which, unsigned long long& sink) {
    long long t0 = now_ns();
    long count = 0;
    switch (which) {
        case 'a': { // 手搓 opendir/readdir(opendir 失败必须查,DIR* 是裸指针)
            unique_dir d{::opendir(kBig)};
            if (!d) {
                std::fprintf(stderr, "opendir(%s) 失败:%s(先跑 ./e4_bench setup)\n", kBig,
                             std::strerror(errno));
                std::exit(1);
            }
            while (dirent* de = ::readdir(d.get())) {
                if (de->d_name[0] == '.' &&
                    (de->d_name[1] == '\0' || (de->d_name[1] == '.' && de->d_name[2] == '\0')))
                    continue;
                ++count;
                sink += std::strlen(de->d_name);
            }
            break;
        }
        case 'b': // directory_iterator,只取名
            for (auto& e : fs::directory_iterator(kBig)) {
                ++count;
                sink += e.path().native().size();
            }
            break;
        case 'c': // + is_regular_file
            for (auto& e : fs::directory_iterator(kBig)) {
                if (e.is_regular_file())
                    ++count;
            }
            break;
        case 'd': { // + file_size
            std::error_code ec;
            for (auto& e : fs::directory_iterator(kBig)) {
                auto sz = e.file_size(ec);
                if (!ec) {
                    ++count;
                    sink += sz;
                }
            }
            break;
        }
    }
    long long t1 = now_ns();
    sink += static_cast<unsigned long long>(count);
    return t1 - t0;
}

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "setup") == 0) {
        setup();
        return 0;
    }
    if (argc > 2 && std::strcmp(argv[1], "one") == 0) {
        unsigned long long sink = 0;
        long long ns = run(argv[2][0], sink);
        std::printf("one %s: %lld us, count/sink=%llu\n", argv[2], ns / 1000, sink);
        return 0;
    }

    const char names[] = "abcd";
    const char* desc[] = {
        "a opendir/readdir(名字)",
        "b directory_iterator(名字)",
        "c dir_iter + is_regular_file",
        "d dir_iter + file_size",
    };
    std::printf("== 10000 文件遍历,%s(预热后 3 轮,CLOCK_MONOTONIC) ==\n", kBig);
    unsigned long long sink = 0;
    for (char w : names)
        run(w, sink); // 预热(目录 inode 进 page cache)

    for (int i = 0; i < 4; ++i) {
        char w = names[i];
        long long med[3];
        for (int r = 0; r < 3; ++r)
            med[r] = run(w, sink);
        std::sort(med, med + 3);
        std::printf("  %c %-32s 轮值 %6.2f %6.2f %6.2f ms -> 中位 %6.2f ms\n", w, desc[i],
                    med[0] / 1e6, med[1] / 1e6, med[2] / 1e6, med[1] / 1e6);
    }
    std::printf("  (sink=%llu,防优化)\n", sink);

    // 顺带:大目录的 readdir 序前 8 名 vs 字典序前 8 名(ext4 大目录转 htree,散列序)
    std::printf("\n== 10000 文件目录的 readdir 序(ext4 htree 散列序,非字典) ==\n  readdir 前8: ");
    int shown = 0;
    for (auto& e : fs::directory_iterator(kBig)) {
        std::printf("%s ", e.path().filename().c_str());
        if (++shown == 8)
            break;
    }
    std::vector<std::string> all;
    for (auto& e : fs::directory_iterator(kBig))
        all.push_back(e.path().filename().string());
    std::sort(all.begin(), all.end());
    std::printf("\n  字典序前8: ");
    for (int i = 0; i < 8; ++i)
        std::printf("%s ", all[i].c_str());
    std::printf(
        "\n  (同一次 readdir 两次调用之间夹了排序,顺序仍稳定:遍历序非随机,是确定的散列序)\n");
    return 0;
}
