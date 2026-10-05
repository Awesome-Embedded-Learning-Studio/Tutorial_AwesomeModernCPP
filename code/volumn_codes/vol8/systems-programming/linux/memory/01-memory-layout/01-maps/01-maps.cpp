// 01-maps —— 进程运行中读自己的 /proc/self/maps:原文 + 逐段归类 + 权限统计
//
// 观察点:
//   1. maps 原文(程序读自己,不存在别人视角的竞态)
//   2. 每段由程序给出归类:本程序的四种 LOAD 段 / [heap] / [stack] /
//      [vdso]/[vvar]/[vsyscall] / 共享库(libc、libstdc++、ld.so…)的段 / 匿名 mmap 区
//   3. 权限组合(r--p / r-xp / rw-p …)计数、总段数、各大类段数
//
// 归类依据:路径字段([heap]/[stack]/[vdso] 是内核起的伪路径)、是否本程序真身
// (对照 /proc/self/exe)、是否匿名(路径为空)、权限与文件偏移(rw 段后紧跟的匿名
// rw 段,典型的 .bss 接续页)。
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

#include <climits>

namespace {

struct Mapping {
    unsigned long start = 0;
    unsigned long end = 0;
    unsigned long offset = 0;
    std::string perms; // 如 r--p / r-xp / rw-p
    std::string path;  // 可为空(匿名)
    std::string raw;   // maps 原始行
};

std::vector<Mapping> read_maps() {
    std::ifstream f("/proc/self/maps");
    std::vector<Mapping> v;
    std::string line;
    while (std::getline(f, line)) {
        Mapping m;
        m.raw = line;
        // 格式:start-end perms offset dev inode pathname
        std::istringstream is(line);
        std::string range, perms, off, dev, inode;
        if (!(is >> range >> perms >> off >> dev >> inode))
            continue;
        auto dash = range.find('-');
        m.start = std::stoul(range.substr(0, dash), nullptr, 16);
        m.end = std::stoul(range.substr(dash + 1), nullptr, 16);
        m.perms = perms;
        m.offset = std::stoul(off, nullptr, 16);
        std::string rest;
        std::getline(is, rest);
        auto p = rest.find_first_not_of(" \t");
        if (p != std::string::npos)
            m.path = rest.substr(p);
        v.push_back(m);
    }
    return v;
}

std::string self_exe() {
    char buf[PATH_MAX];
    ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    return (n > 0) ? std::string(buf, n) : std::string();
}

std::string base_name(const std::string& p) {
    auto d = p.rfind('/');
    return (d == std::string::npos) ? p : p.substr(d + 1);
}

// 只读/可执行/可写子段名(用于程序与共享库的段内细分)
// 同一文件会出现两个 r-- 段:偏移居中的是 .rodata;紧贴 rw 段的那个是
// GNU_RELRO(.got/.dynamic 等,动态链接器做完重定位后 mprotect 成只读)
std::string subseg(const std::vector<Mapping>& ms, size_t i) {
    const Mapping& m = ms[i];
    if (m.perms.rfind("r-x", 0) == 0)
        return "代码段(.text)";
    if (m.perms.rfind("rw-", 0) == 0)
        return "可写数据段(.data/.bss 前半,尾部常接 .bss 匿名页)";
    if (i + 1 < ms.size() && ms[i + 1].path == m.path && ms[i + 1].perms.rfind("rw-", 0) == 0)
        return "GNU_RELRO(重定位完成后由 rw 转只读的 .got/.dynamic)";
    if (m.offset == 0)
        return "ELF 头+链接器元数据(只读 LOAD 首段)";
    return "只读数据段(.rodata)";
}

// 归类:返回 "大类|细分说明",大类用于统计
std::string classify(const std::vector<Mapping>& ms, size_t i) {
    const Mapping& m = ms[i];
    const std::string& p = m.path;
    if (p == "[heap]")
        return "堆|[heap](brk 管辖,向高地址生长)";
    if (p == "[stack]")
        return "栈|[stack](主线程栈,向低地址生长)";
    if (p == "[vdso]")
        return "vdso|[vdso] 内核映射的虚拟共享对象(系统调用加速桩)";
    if (p.rfind("[vvar", 0) == 0)
        return "vdso|" + p + " 内核只读数据页(vdso 配套,如时钟源)";
    if (p == "[vsyscall]")
        return "vdso|[vsyscall] 传统 vsyscall 页(存在与否取决于内核参数)";
    if (p.empty()) {
        if (i > 0) {
            const Mapping& prev = ms[i - 1];
            const std::string& pp = prev.path;
            if (!pp.empty() && pp[0] != '[' && prev.perms.rfind("rw-", 0) == 0)
                return "匿名|匿名 rw 段(紧邻 " + base_name(pp) + " 的 rw 段,典型的 .bss 接续页)";
        }
        return "匿名|匿名映射(无文件背书,mmap 区)";
    }
    static const std::string exe = self_exe();
    if (p.rfind(exe, 0) == 0)
        return "本程序|本程序·" + subseg(ms, i);
    if (p == "/etc/ld.so.cache")
        return "共享库|ld.so 缓存(动态链接器查库用的文件映射)";
    std::string tag;
    if (p.find("libc.so") != std::string::npos)
        tag = "共享库 libc";
    else if (p.find("libstdc++.so") != std::string::npos)
        tag = "共享库 libstdc++";
    else if (p.find("libm.so") != std::string::npos)
        tag = "共享库 libm";
    else if (p.find("libgcc_s.so") != std::string::npos)
        tag = "共享库 libgcc_s";
    else if (p.find("ld-linux") != std::string::npos || p.find("ld.so") != std::string::npos)
        tag = "动态链接器 ld.so";
    else
        tag = "文件映射";
    return "共享库|" + tag + "·" + subseg(ms, i);
}

} // namespace

int main() {
    auto maps = read_maps();

    std::printf("== /proc/self/maps 原文(共 %zu 行,程序运行中自读) ==\n", maps.size());
    for (const auto& m : maps)
        std::printf("%s\n", m.raw.c_str());

    std::printf("\n== 逐段归类(同行原文 → 程序判定) ==\n");
    std::map<std::string, int> cat_cnt;  // 大类计数
    std::map<std::string, int> perm_cnt; // 权限组合计数
    for (size_t i = 0; i < maps.size(); ++i) {
        const auto& m = maps[i];
        std::string c = classify(maps, i);
        auto bar = c.find('|');
        ++cat_cnt[c.substr(0, bar)];
        ++perm_cnt[m.perms];
        std::printf("%s\n    -> [%4lu KB] %s\n", m.raw.c_str(), (m.end - m.start) / 1024,
                    c.substr(bar + 1).c_str());
    }

    std::printf("\n== 权限组合统计 ==\n");
    for (const auto& [perm, n] : perm_cnt)
        std::printf("  %-5s x %d\n", perm.c_str(), n);
    std::printf("  总段数: %zu\n", maps.size());

    std::printf("\n== 大类统计 ==\n");
    for (const auto& [cat, n] : cat_cnt)
        std::printf("  %-8s x %d\n", cat.c_str(), n);
    return 0;
}
