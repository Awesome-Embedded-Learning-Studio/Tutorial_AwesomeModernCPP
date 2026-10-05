// e1_loop.cpp —— E1 续:follow_directory_symlink 与符号链接环
//
// 树:e1_loop/keep.txt + e1_loop/loop/inner,其中 inner 是指向 ".." 的符号链接。
// 链接目标按 inner 所在的 loop/ 解析,.. 落在树根 e1_loop(不是 loop 自身),
// 物理上 loop/inner -> 根目录,递归跟随即成环(根 -> loop -> inner -> 回根)。
//  [默认]      directory_options::none:目录链接只报名不下降,安全终止。
//  [follow]    follow_directory_symlink:标准不要求检测环;libstdc++ 实测检不检?
//              程序里设 2000 步硬上限,跑满即证明「无环检测,一直转」。
// 编译:g++ -std=c++20 -Wall -Wextra -O2 -I ../common e1_loop.cpp -o e1_loop
#include <cstdio>
#include <filesystem>

namespace fs = std::filesystem;

static const char* kRoot = "/home/charliechen/l04_scratch/e1_loop";

static void build() {
    fs::remove_all(kRoot);
    fs::create_directories(fs::path(kRoot) / "loop");
    if (FILE* f = std::fopen((fs::path(kRoot) / "keep.txt").c_str(), "w")) {
        std::fputs("k\n", f);
        std::fclose(f);
    }
    // inner -> .. :解析时以 inner 所在目录(loop/)为基准,.. 落在树根 e1_loop(环是
    // 根->loop->inner->回根)
    fs::create_directory_symlink("..", fs::path(kRoot) / "loop/inner");
}

int main() {
    build();

    std::printf("== 默认选项:directory_options::none ==\n");
    int n = 0;
    for (fs::recursive_directory_iterator it(kRoot), end; it != end; ++it) {
        auto& e = *it; // depth() 在迭代器上
        std::printf("  depth=%d %s (symlink=%s)\n", static_cast<int>(it.depth()),
                    e.path().lexically_relative(kRoot).c_str(), e.is_symlink() ? "Y" : "n");
        ++n;
    }
    std::printf("  共 %d 条,正常终止(目录链接不下降,环根本没被走进去)\n\n", n);

    std::printf("== follow_directory_symlink ==\n");
    auto opts = fs::directory_options::follow_directory_symlink;
    fs::recursive_directory_iterator it(kRoot, opts), end;
    long steps = 0, keep_hits = 0;
    const long kCap = 2000; // 硬上限:防止真无限循环挂死实验
    int shown = 0;
    std::error_code last_ec;
    while (it != end) {
        ++steps;
        if (it->path().filename() == "keep.txt")
            ++keep_hits;
        if (shown < 8 || steps % 100 == 0) { // 只打头几条和里程碑,输出别爆炸
            std::printf("  step=%-5ld depth=%-3d path=%s\n", steps, static_cast<int>(it.depth()),
                        it->path().lexically_relative(kRoot).c_str());
            ++shown;
        }
        if (steps == kCap) {
            std::printf("  ...硬上限 %ld 步打住。path 长度已涨到 %zu 字符,depth=%d\n", kCap,
                        it->path().string().size(), static_cast<int>(it.depth()));
            break;
        }
        last_ec.clear();
        it.increment(last_ec);
        if (last_ec) {
            std::printf("  increment 出错:%d (%s),第 %ld 步,当时 path 长度 %zu\n", last_ec.value(),
                        last_ec.message().c_str(), steps,
                        it != end ? it->path().string().size() : static_cast<std::size_t>(0));
            break;
        }
    }
    std::printf("  退出时状态:steps=%ld,it==end ? %s, last_ec.value()=%d\n", steps,
                it == end ? "yes" : "no", last_ec.value());
    if (it != end) {
        std::printf("  停止时的完整 path(%zu 字符):%s\n", it->path().string().size(),
                    it->path().c_str());
    }
    std::printf(steps >= kCap
                    ? "  结论:跑满 %ld 步上限也没停 —— libstdc++ 不检测环,follow 模式下无限递归\n"
                    : "  结论:%ld 步停了,keep.txt(全树就一个真文件)被访问 %ld 次 —— "
                      "同一个环转了 %ld 圈,遍历结果已被污染\n",
                steps, keep_hits, keep_hits);
    std::printf("  停下的机制(strace 实证,e1_loop_strace.txt):迭代器下降用的是"
                "openat(父fd, 名字),单步只解析 1 个符号链接,永远撞不上环检测;"
                "但每个条目的类型判定要对「完整路径」newfstatat,路径里内嵌符号链接"
                "≥40 个时内核返回 ELOOP —— 这一声 ELOOP 被库吞掉(不设 ec),"
                "下降静默停止,DFS 干净收尾到 end()。三个要点:\n"
                "    1) libstdc++ 自己不做环检测(标准也留白 unspecified);\n"
                "    2) 救命的是内核 40 层符号链接上限,拦的还是属性查询那条路;\n"
                "    3) 全程 ec 干净、不抛不挂 —— 「没报错」不等于「结果对」。\n");
    return 0;
}
