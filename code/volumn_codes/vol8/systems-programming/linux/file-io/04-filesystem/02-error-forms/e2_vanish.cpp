// e2_vanish.cpp —— E2 续:迭代进行中,目录被外部 rm -rf 掉,迭代器什么反应
//
// 双进程编排(删除来自另一个进程,才是「外部」):
//   子进程:建树 -> 慢速迭代(每条 usleep) -> 第 10 条后放 marker
//   父进程:见 marker -> sleep 一下 -> system("rm -rf ...")
// 两个阶段:
//   [A] directory_iterator 大目录(3000 文件,readdir 缓冲要好几轮才装得下):
//       删除后 ++ 还灵不灵?条目名还吐不吐?属性查询撞什么墙?
//   [B] recursive_directory_iterator 小树:下降打开子目录时子目录已消失,
//       increment(ec) 报什么?
// 基线对照:./e2_vanish solo  —— 不删,全程走完的条目数
// 编译:g++ -std=c++20 -Wall -Wextra -O2 e2_vanish.cpp -o e2_vanish
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <system_error>
#include <thread>
#include <unistd.h>

#include <sys/wait.h>

namespace fs = std::filesystem;

static const char* kLive = "/home/charliechen/l04_scratch/e2_live";
static const char* kCoord = "/home/charliechen/l04_scratch/e2/coord";

static void touch(const char* p) {
    if (FILE* f = std::fopen(p, "w"))
        std::fclose(f);
}
static bool exists(const char* p) {
    return ::access(p, F_OK) == 0;
}

// 大目录:3000 个顶层文件(先建)+ 1 个子目录 5 个文件(后建)
static void build_big() {
    fs::remove_all(kLive);
    fs::create_directories(kLive);
    char name[32];
    for (int i = 0; i < 3000; ++i) {
        std::snprintf(name, sizeof name, "f%04d", i);
        if (FILE* f = std::fopen((fs::path(kLive) / name).c_str(), "w"))
            std::fclose(f);
    }
    fs::create_directories(fs::path(kLive) / "sub_late");
    for (int i = 0; i < 5; ++i) {
        std::snprintf(name, sizeof name, "s%d", i);
        if (FILE* f = std::fopen((fs::path(kLive) / "sub_late" / name).c_str(), "w"))
            std::fclose(f);
    }
}

// 小树:20 个文件 + 两个子目录
static void build_small() {
    fs::remove_all(kLive);
    fs::create_directories(fs::path(kLive) / "sub_a");
    fs::create_directories(fs::path(kLive) / "sub_b");
    char name[32];
    for (int i = 0; i < 20; ++i) {
        std::snprintf(name, sizeof name, "t%02d", i);
        if (FILE* f = std::fopen((fs::path(kLive) / name).c_str(), "w"))
            std::fclose(f);
    }
    for (const char* sub : {"sub_a", "sub_b"})
        for (int i = 0; i < 5; ++i) {
            std::snprintf(name, sizeof name, "u%d", i);
            if (FILE* f = std::fopen((fs::path(kLive) / sub / name).c_str(), "w"))
                std::fclose(f);
        }
}

// ---- 阶段 A:大目录,迭代中被删 ----
static void phase_a(bool really_delete) {
    build_big();
    std::printf("[A] directory_iterator,%s删除\n", really_delete ? "迭代中外部" : "不");
    long steps = 0, attr_ok = 0, attr_fail = 0, incr_fail = 0;
    fs::directory_iterator it{kLive}; // 现在还活着,正常构造
    for (fs::directory_iterator end; it != end;) {
        ++steps;
        if (really_delete && steps == 10)
            touch((fs::path(kCoord) / "marker_a").c_str());
        std::error_code attr_ec;
        auto sz = it->file_size(attr_ec); // 不缓存的属性(e1_cache 证据:每条一次 stat)
        if (!attr_ec && sz == 0)
            ++attr_ok;
        else if (attr_ec)
            ++attr_fail;
        if (steps <= 6 || steps % 200 == 0)
            std::printf("  step=%-5ld name=%-10s attr_ec=%d(%s)\n", steps,
                        it->path().filename().c_str(), attr_ec.value(), attr_ec.message().c_str());
        if (really_delete)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        std::error_code inc_ec;
        it.increment(inc_ec);
        if (inc_ec) {
            ++incr_fail;
            std::printf("  step=%ld increment 失败:%d %s\n", steps, inc_ec.value(),
                        inc_ec.message().c_str());
            break;
        }
    }
    std::printf("  [A 小结] 共见 %ld 条(基线应为 3001);increment 失败 %ld 次;"
                "属性查询成功 %ld、失败 %ld\n",
                steps, incr_fail, attr_ok, attr_fail);
    std::printf("  树还在吗:%s\n", fs::exists(kLive) ? "在" : "已被 rm -rf");
}

// ---- 阶段 B:递归下降撞上已删子目录 ----
static void phase_b(bool really_delete) {
    build_small();
    std::printf("\n[B] recursive_directory_iterator,%s删除\n", really_delete ? "迭代中外部" : "不");
    long steps = 0;
    fs::recursive_directory_iterator it{kLive};
    for (fs::recursive_directory_iterator end; it != end;) {
        ++steps;
        if (really_delete && steps == 2)
            touch((fs::path(kCoord) / "marker_b").c_str());
        std::printf("  step=%-4ld depth=%d %s\n", steps, static_cast<int>(it.depth()),
                    it->path().lexically_relative(kLive).c_str());
        if (really_delete)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        std::error_code inc_ec;
        it.increment(inc_ec);
        if (inc_ec) {
            std::printf("  step=%ld increment 失败:%d (%s) —— 标准把这算错误,"
                        "递归下降打不开子目录\n",
                        steps, inc_ec.value(), inc_ec.message().c_str());
            break;
        }
    }
    std::printf("  [B 小结] 共见 %ld 条(基线应为 32 = 20 文件 + 2 目录 + 10 子文件);树还在吗:%s\n",
                steps, fs::exists(kLive) ? "在" : "已被 rm -rf");
}

static int run_child(bool really_delete) {
    phase_a(really_delete);
    if (really_delete)
        touch((fs::path(kCoord) / "done_a").c_str());
    phase_b(really_delete);
    if (really_delete)
        touch((fs::path(kCoord) / "done_b").c_str());
    return 0;
}

int main(int argc, char** argv) {
    fs::create_directories(kCoord);
    if (argc > 1 && std::strcmp(argv[1], "solo") == 0) {
        run_child(false); // 基线:没人删
        return 0;
    }

    fs::remove_all(kCoord);
    fs::create_directories(kCoord);
    pid_t pid = ::fork();
    if (pid == 0)
        return run_child(true); // 子进程:迭代方

    // 父进程:外部删除方
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    std::printf("[parent] 等子进程 marker_a ...\n");
    while (!exists((fs::path(kCoord) / "marker_a").c_str()))
        usleep(2000);
    usleep(150000); // 让子进程再多走几条,踩进 readdir 缓冲中段
    std::printf("[parent] rm -rf %s(阶段 A)\n", kLive);
    std::system(("rm -rf " + std::string(kLive)).c_str());
    while (!exists((fs::path(kCoord) / "done_a").c_str()))
        usleep(2000);

    while (!exists((fs::path(kCoord) / "marker_b").c_str()))
        usleep(2000);
    usleep(20000); // 小树 32 条、子进程 5ms/条,删早一点才落在迭代中段
    std::printf("[parent] rm -rf %s(阶段 B)\n", kLive);
    std::system(("rm -rf " + std::string(kLive)).c_str());

    int st = 0;
    ::waitpid(pid, &st, 0);
    std::printf("[parent] child exit status=%d,实验收尾\n", st);
    return 0;
}
