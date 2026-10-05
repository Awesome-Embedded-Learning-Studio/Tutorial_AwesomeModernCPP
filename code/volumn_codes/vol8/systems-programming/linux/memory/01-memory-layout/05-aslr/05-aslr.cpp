// 05-aslr —— 同一程序多次运行,观察 ASLR 对各段基址的随机化;setarch -R 对照
//
// 每次运行打印一行:程序加载基址 / [heap] 起点 / [stack] 起止 / libc 基址 /
// vvar / vdso。外部对照由复现命令完成:
//   普通跑 5 次 → 每次地址都不同(ASLR 开)
//   setarch -R 跑 5 次 → 每次完全相同(ADDR_NO_RANDOMIZE)
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>

#include <climits>

namespace {

// 从 /proc/self/maps 找第一条命中段的起始地址;mode: 0=路径全等 1=路径包含 2=路径前缀
unsigned long find_first(int mode, const std::string& key) {
    std::ifstream f("/proc/self/maps");
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream is(line);
        std::string range, perms, off, dev, inode, path;
        if (!(is >> range >> perms >> off >> dev >> inode >> path))
            continue;
        bool hit = (mode == 0 && path == key) ||
                   (mode == 1 && path.find(key) != std::string::npos) ||
                   (mode == 2 && path.rfind(key, 0) == 0);
        if (hit) {
            auto dash = range.find('-');
            return std::stoul(range.substr(0, dash), nullptr, 16);
        }
    }
    return 0;
}

unsigned long stack_top() { // [stack] 的 end(最高地址)
    std::ifstream f("/proc/self/maps");
    std::string line;
    while (std::getline(f, line)) {
        if (line.find("[stack]") != std::string::npos) {
            auto dash = line.find('-');
            return std::stoul(line.substr(dash + 1, 12), nullptr, 16);
        }
    }
    return 0;
}

} // namespace

int main() {
    char exe[PATH_MAX];
    ssize_t n = ::readlink("/proc/self/exe", exe, sizeof exe - 1);
    exe[n > 0 ? n : 0] = '\0';

    std::printf("exe=%012lx heap=%012lx stack=%012lx-%012lx libc=%012lx vvar=%012lx vdso=%012lx\n",
                find_first(0, exe),       // 程序加载基址(PIE)
                find_first(0, "[heap]"),  // [heap] 起点
                find_first(0, "[stack]"), // [stack] 起点(低地址端)
                stack_top(),              // [stack] 顶端
                find_first(1, "libc.so"), // libc 基址
                find_first(2, "[vvar"),   // [vvar] 家族
                find_first(0, "[vdso]")); // [vdso]
    return 0;
}
