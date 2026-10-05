// e1_trio.cpp —— E1:三种目录遍历对同一棵树各走一遍,比对顺序
//
// 测试树 5 层。实测(本目录 .out 与 e1_trio_lsu.txt):ext4 返回的是「按名字散列」的
// 确定序 —— 与创建顺序无关(先建的 mid 排第 4)、与字典序无关(逐个核对过),
// 但对同一批名字,跨进程跨工具(/usr/bin/ls -U、find、python os.listdir)完全一致。
// 注意:shell 里 ls 常是 eza 的别名,-U 语义不同,对拍要用 /usr/bin/ls -U。
// 三件套:fs::directory_iterator(单层) / fs::recursive_directory_iterator(默认选项)
//        / 手搓 opendir+readdir(POSIX 底层)。
// 编译:g++ -std=c++20 -Wall -Wextra -O2 -I ../common e1_trio.cpp -o e1_trio
#include "fsio.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

static const char* kRoot = "/home/charliechen/l04_scratch/e1_tree";

static void put(const fs::path& p, const char* text) {
    if (FILE* f = std::fopen(p.c_str(), "w")) {
        std::fputs(text, f);
        std::fclose(f);
    }
}

// 按固定脚本建树;重复运行先删再建,保证顺序稳定
static void build_tree() {
    fs::remove_all(kRoot);
    fs::create_directories(fs::path(kRoot) / "mid/deep1/deep2/deep3/deep4");
    put(fs::path(kRoot) / "zeta.txt", "z\n");    // 1st
    put(fs::path(kRoot) / ".hidden", "h\n");     // 2nd
    put(fs::path(kRoot) / "mid/yan.txt", "y\n"); // mid 先于 alpha
    put(fs::path(kRoot) / "mid/.h2", "2\n");
    put(fs::path(kRoot) / "mid/deep1/xray.txt", "x\n");
    put(fs::path(kRoot) / "mid/deep1/deep2/deep3/deep4/bottom.txt", "b\n");
    put(fs::path(kRoot) / "alpha.txt", "a\n"); // last
    fs::create_directory_symlink("mid", fs::path(kRoot) / "link_mid");
    fs::create_symlink("zeta.txt", fs::path(kRoot) / "link_zeta");
    fs::create_symlink("no_such_target", fs::path(kRoot) / "broken_link");
}

int main() {
    build_tree();
    std::printf("tree root: %s  (mid 先建、alpha.txt 最后建;readdir 吐的序两者都不像)\n\n", kRoot);

    // 坑位记录:fs::relative() 走 weakly_canonical,会把符号链接解析成目标路径,
    // 展示迭代器吐出的原始路径要用 lexically_relative()(纯词法,不碰盘上的链接)

    // ---- [A] fs::directory_iterator:只走本层 ----
    std::printf("== [A] fs::directory_iterator(单层,默认) ==\n");
    std::vector<std::string> order_a;
    for (auto& e : fs::directory_iterator(kRoot)) {
        // is_symlink() 用的是 readdir 缓存的类型;is_directory() 跟随链接(status 语义)
        const char* t = e.is_symlink() ? (e.is_directory() ? "link->dir " : "link->file")
                                       : (e.is_directory() ? "dir" : "file");
        std::printf("  %-14s type=%s\n", e.path().filename().c_str(), t);
        order_a.push_back(e.path().filename().string());
    }

    // ---- [B] fs::recursive_directory_iterator:深度优先,默认不跟随目录链接 ----
    std::printf("\n== [B] fs::recursive_directory_iterator(默认 directory_options::none) ==\n");
    std::vector<std::string> order_b;
    for (fs::recursive_directory_iterator it(kRoot), end; it != end; ++it) {
        auto& e = *it; // depth() 在迭代器上,不在 directory_entry 上
        const char* t = e.is_symlink() ? (e.is_directory() ? "link->dir " : "link->file")
                                       : (e.is_directory() ? "dir" : "file");
        std::printf("  depth=%d %-52s type=%s\n", static_cast<int>(it.depth()),
                    e.path().lexically_relative(kRoot).c_str(), t);
        order_b.push_back(e.path().lexically_relative(kRoot).string());
    }
    std::printf("  (link_mid 是指向目录的符号链接:报了名,但默认不下降,其下没有条目)\n");

    // ---- [C] 手搓 opendir+readdir:POSIX 底层,含 . 与 .. ----
    std::printf("\n== [C] opendir+readdir(顶层,原始 d_name 序,含 . 和 ..) ==\n");
    std::vector<std::string> order_c;
    unique_dir d{opendir(kRoot)};
    if (!d)
        return 1;
    while (dirent* de = ::readdir(d.get())) {
        std::printf("  d_name=%-14s d_type=%s\n", de->d_name,
                    de->d_type == DT_DIR       ? "DT_DIR"
                    : de->d_type == DT_LNK     ? "DT_LNK"
                    : de->d_type == DT_REG     ? "DT_REG"
                    : de->d_type == DT_UNKNOWN ? "DT_UNKNOWN"
                                               : "other");
        if (std::strcmp(de->d_name, ".") != 0 && std::strcmp(de->d_name, "..") != 0)
            order_c.push_back(de->d_name);
    }

    // ---- 对照:字典序 ----
    std::printf("\n== [D] std::sort 后的字典序(对照) ==\n  ");
    std::vector<std::string> sorted = order_a;
    std::sort(sorted.begin(), sorted.end());
    for (auto& s : sorted)
        std::printf("%s ", s.c_str());
    std::printf("\n");

    // ---- 机器判等:A 与 C 同序?A 是不是字典序? ----
    std::printf("\n== 判定 ==\n");
    std::printf("directory_iterator 顶层序 == readdir 序(去 . ..): %s\n",
                order_a == order_c ? "yes" : "NO");
    std::printf("directory_iterator 顶层序 == 字典序:              %s\n",
                order_a == sorted ? "yes(碰巧)" : "no(非排序)");
    // [B] 的 depth-0 条目(回抬到顶层时的产出序)应与 [A] 完全同序:
    // 递归版就是 DFS 先序,顶层兄弟间的相对顺序不变
    std::vector<std::string> top_of_b;
    for (fs::recursive_directory_iterator it(kRoot), end; it != end; ++it)
        if (it.depth() == 0)
            top_of_b.push_back(it->path().lexically_relative(kRoot).string());
    std::printf("recursive 的 depth-0 条目序 == [A] 顶层序:        %s\n",
                order_a == top_of_b ? "yes(DFS 先序,顶层兄弟序不动)" : "NO");
    return 0;
}
