// e1_cache.cpp —— E1 续:directory_entry 缓存的属性 vs 每次现查
//
// 三种姿势跑同一个 200 文件的目录,各自单独跑一次进程,syscall 数量交给 strace 数:
//   ./e1_cache setup   # 建 e1_cache_dir/f000..f199(内容 abc)
//   ./e1_cache type    # entry.is_directory()/is_regular_file():readdir 的 d_type 缓存?
//   ./e1_cache size    # entry.file_size(ec):迭代时有没有顺带缓存大小?
//   ./e1_cache fresh   # fs::is_regular_file(entry.path()):按路径现查(对照)
// 外层用 strace 分别数 statx/newfstatat 次数,证据见 e1_cache_strace_type.txt 与 e1_cache.out 内的
// strace -c 计数 编译:g++ -std=c++20 -Wall -Wextra -O2 -I ../common e1_cache.cpp -o e1_cache
#include <cstdio>
#include <filesystem>

namespace fs = std::filesystem;

static const char* kDir = "/home/charliechen/l04_scratch/e1_cache_dir";

int main(int argc, char** argv) {
    std::string mode = argc > 1 ? argv[1] : "type";
    if (mode == "setup") {
        fs::remove_all(kDir);
        fs::create_directories(kDir);
        for (int i = 0; i < 200; ++i) {
            char name[32];
            std::snprintf(name, sizeof name, "f%03d", i);
            if (FILE* f = std::fopen((fs::path(kDir) / name).c_str(), "w")) {
                std::fputs("abc", f);
                std::fclose(f);
            }
        }
        std::printf("setup: %s 下 200 个 3 字节文件\n", kDir);
        return 0;
    }

    long dirs = 0, regs = 0, other = 0;
    unsigned long long sink = 0;
    if (mode == "type") { // directory_entry 的类型判定
        for (auto& e : fs::directory_iterator(kDir)) {
            if (e.is_directory())
                ++dirs;
            else if (e.is_regular_file())
                ++regs;
            else
                ++other;
        }
    } else if (mode == "size") { // directory_entry::file_size
        for (auto& e : fs::directory_iterator(kDir)) {
            std::error_code ec;
            auto sz = e.file_size(ec);
            if (!ec)
                sink += sz;
        }
    } else { // fs::is_regular_file(按 entry 的路径现查)
        for (auto& e : fs::directory_iterator(kDir)) {
            if (fs::is_regular_file(e.path()))
                ++regs;
            else if (fs::is_directory(e.path()))
                ++dirs;
            else
                ++other;
        }
    }
    // sink 防优化:把结果打出来
    std::printf("mode=%-6s dirs=%ld regs=%ld other=%ld size_sum=%llu\n", mode.c_str(), dirs, regs,
                other, sink);
    return 0;
}
