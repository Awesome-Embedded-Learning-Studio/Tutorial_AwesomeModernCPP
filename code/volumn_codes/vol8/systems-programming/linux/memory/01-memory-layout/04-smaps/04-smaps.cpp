// 04-smaps —— /proc/self/smaps 关键字段解读,maps 同段对照 + pmap -x 外部工具
//
// maps 一行只给「地址范围/权限/偏移/文件」;smaps 是同一批 VMA 逐块展开的
// 内存账本。本程序给四个代表性段贴 maps+smaps 两份原文,再从 smaps 抽
// Rss/Pss/Shared_*/Private_*/THP 相关字段做对照表,最后由外部工具 pmap -x
// 对同一进程再数一遍。
#include <sys/mman.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

#include <climits>

namespace {

struct Mapping {
    unsigned long start = 0, end = 0, offset = 0;
    std::string perms, path, raw;
    std::vector<std::pair<std::string, std::string>> fields; // smaps 专属字段
};

std::string self_exe() {
    char buf[PATH_MAX];
    ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    return (n > 0) ? std::string(buf, n) : std::string();
}

bool parse_header(const std::string& line, Mapping& m) {
    // 形如 "start-end perms offset dev inode [path]" 的行是块头
    std::istringstream is(line);
    std::string range, perms, off, dev, inode;
    if (!(is >> range >> perms >> off >> dev >> inode))
        return false;
    if (range.find('-') == std::string::npos)
        return false;
    unsigned long s, e;
    try {
        auto dash = range.find('-');
        s = std::stoul(range.substr(0, dash), nullptr, 16);
        e = std::stoul(range.substr(dash + 1), nullptr, 16);
    } catch (...) {
        return false;
    }
    for (char c : perms)
        if (c != 'r' && c != 'w' && c != 'x' && c != '-' && c != 'p' && c != 's')
            return false;
    m.start = s;
    m.end = e;
    m.perms = perms;
    m.raw = line;
    m.offset = std::stoul(off, nullptr, 16);
    std::string rest;
    std::getline(is, rest);
    auto p = rest.find_first_not_of(" \t");
    if (p != std::string::npos)
        m.path = rest.substr(p);
    return true;
}

// 读 smaps:块头与 maps 同格式,其后每行一个 "Key:  Value" 字段
std::vector<Mapping> read_smaps() {
    std::ifstream f("/proc/self/smaps");
    std::vector<Mapping> v;
    std::string line;
    Mapping cur;
    bool open = false;
    while (std::getline(f, line)) {
        Mapping m;
        if (parse_header(line, m)) {
            if (open)
                v.push_back(cur);
            cur = m;
            cur.fields.clear();
            open = true;
        } else if (open) {
            auto colon = line.rfind(": ");
            if (colon != std::string::npos)
                cur.fields.emplace_back(line.substr(0, colon), line.substr(colon + 2));
        }
    }
    if (open)
        v.push_back(cur);
    return v;
}

std::string field(const Mapping& m, const char* key) {
    for (const auto& [k, val] : m.fields)
        if (k == key)
            return val;
    return "?";
}

} // namespace

int main() {
    // 先制造可观察的内存状态:堆上 64×32KB(每块都低于 mmap 阈值,必来自 brk)
    // 全部写脏;栈上摸 256KB。
    std::vector<char*> heap_chunks;
    for (int i = 0; i < 64; ++i) {
        char* p = (char*)std::malloc(32 * 1024);
        for (int j = 0; j < 32 * 1024; j += 4096)
            p[j] = (char)j; // 每页都写
        heap_chunks.push_back(p);
    }
    volatile char stack_touch[256 * 1024];
    for (int j = 0; j < 256 * 1024; j += 4096)
        stack_touch[j] = (char)j;
    volatile char stack_witness = stack_touch[0]; // 读一次,防「只写不读」
    (void)stack_witness;

    const std::string exe = self_exe();
    auto smaps = read_smaps();

    // 选四个代表段:本程序 .text / [heap] / [stack] / libc .text
    auto find_seg = [&](const char* want) -> const Mapping* {
        for (const auto& m : smaps) {
            if (std::string(want) == "exe_text" && m.path == exe && m.perms.rfind("r-x", 0) == 0)
                return &m;
            if (std::string(want) == "heap" && m.path == "[heap]")
                return &m;
            if (std::string(want) == "stack" && m.path == "[stack]")
                return &m;
            if (std::string(want) == "libc_text" && m.path.find("libc.so") != std::string::npos &&
                m.perms.rfind("r-x", 0) == 0)
                return &m;
        }
        return nullptr;
    };

    const char* names[] = {"exe_text", "heap", "stack", "libc_text"};
    const char* titles[] = {"本程序 .text(代码段,文件映射,只读+可执行)",
                            "[heap](堆,匿名私有,全部被写脏)", "[stack](主线程栈,匿名私有)",
                            "libc .text(共享库代码段,多进程共享的主体)"};
    std::printf("== 四个代表段:maps 视角与 smaps 展开(smaps 块头与 maps 行同格式) ==\n");
    for (int i = 0; i < 4; ++i) {
        const Mapping* m = find_seg(names[i]);
        std::printf("\n### 段 %d:%s\n", i + 1, titles[i]);
        if (!m) {
            std::printf("  (未找到)\n");
            continue;
        }
        std::printf("maps 行同款块头:%s\n", m->raw.c_str());
        std::printf("smaps 整块:\n");
        std::printf("  %s\n", m->raw.c_str());
        for (const auto& [k, val] : m->fields)
            std::printf("  %-24s %s\n", k.c_str(), val.c_str());
    }

    std::printf("\n== 关键字段对照(单位 kB;? 表示该段无此字段) ==\n");
    std::printf("%-14s %8s %8s %8s %8s %8s %8s %8s %8s %8s\n", "段", "Size", "Rss", "Pss", "Sh_Cln",
                "Sh_Dirty", "Pr_Cln", "Pr_Dirty", "AnonHuge", "THPelig");
    const char* rows[] = {"本程序.text", "[heap]", "[stack]", "libc.text"};
    for (int i = 0; i < 4; ++i) {
        const Mapping* m = find_seg(names[i]);
        if (!m)
            continue;
        std::printf("%-14s %8s %8s %8s %8s %8s %8s %8s %8s %8s\n", rows[i],
                    field(*m, "Size").c_str(), field(*m, "Rss").c_str(), field(*m, "Pss").c_str(),
                    field(*m, "Shared_Clean").c_str(), field(*m, "Shared_Dirty").c_str(),
                    field(*m, "Private_Clean").c_str(), field(*m, "Private_Dirty").c_str(),
                    field(*m, "AnonHugePages").c_str(), field(*m, "THPeligible").c_str());
    }
    std::printf("字段口径:Rss=实际驻留;Pss=按共享比例摊派后的\"这支进程的真实 footprint\";\n"
                "Shared/Private 按是否与其他进程共享分账,Clean/Dirty 按是否被写过(或换出过)分账;\n"
                "AnonHugePages/THPeligible 是透明大页(THP)口径。\n");

    std::printf("\n== 外部工具对照:pmap -x %d(此刻同一进程) ==\n", (int)getpid());
    std::fflush(stdout); // 先冲净自己的缓冲,再让 pmap 直接写 stdout,避免交錯
    std::string cmd = "pmap -x " + std::to_string(getpid());
    int rc = std::system(cmd.c_str());
    std::printf("(pmap 退出码 %d;pmap -x 的 Kbytes/RSS 列与 smaps 的 Size/Rss 同源,"
                "Dirty 列是 pmap 按 pagemap 自家口径算的,与 smaps 的 Private_Dirty 不必逐行相等)\n",
                rc);
    return 0;
}
