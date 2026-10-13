// 02-var-locations —— 各类变量的地址,与同一运行里的 /proc/self/maps 对表
//
// 观察点:全局已初始化(.data)/ 全局未初始化(.bss)/ const 常量与字符串字面量
// (.rodata)/ static 局部(初始化→.data,未初始化→.bss)/ 栈变量 / malloc 与 new
// ([heap] 或大块走匿名 mmap)/ mmap 直配(匿名区)/ 本程序函数地址(.text)/
// libc 函数地址(libc 的 .text)。
//
// ASLR 只影响「不同运行之间」;本程序在同一次运行内先取地址、再读 maps、
// 再逐个对表,因此对表是自洽的。
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
};

std::vector<Mapping> read_maps() {
    std::ifstream f("/proc/self/maps");
    std::vector<Mapping> v;
    std::string line;
    while (std::getline(f, line)) {
        Mapping m;
        m.raw = line;
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

// 段归类 token(程序判定)
enum Tok { OTHER, TEXT, RODATA, DATA_RW, BSS_ANON, HEAP, STACK, ANON, LIBC_TEXT };

struct SegInfo {
    Tok tok = OTHER;
    std::string desc; // 人读描述
};

SegInfo locate(const std::vector<Mapping>& ms, size_t i) {
    const Mapping& m = ms[i];
    const std::string& p = m.path;
    static const std::string exe = self_exe();
    SegInfo s;
    if (p == "[heap]") {
        s.tok = HEAP;
        s.desc = "[heap] 堆(brk)";
        return s;
    }
    if (p == "[stack]") {
        s.tok = STACK;
        s.desc = "[stack] 栈";
        return s;
    }
    if (p.empty()) {
        if (i > 0 && ms[i - 1].path == exe && ms[i - 1].perms.rfind("rw-", 0) == 0) {
            s.tok = BSS_ANON;
            s.desc = "匿名 rw 段,紧贴本程序 rw 段 → .bss 接续页";
        } else {
            s.tok = ANON;
            s.desc = "匿名映射(mmap 区)";
        }
        return s;
    }
    if (p.rfind(exe, 0) == 0) {
        if (m.perms.rfind("r-x", 0) == 0) {
            s.tok = TEXT;
            s.desc = "本程序 .text";
        } else if (m.perms.rfind("rw-", 0) == 0) {
            s.tok = DATA_RW;
            s.desc = "本程序 rw 段(.data+.bss 尾页)";
        } else if (m.offset == 0) {
            s.tok = RODATA;
            s.desc = "本程序只读首段(ELF 头/元数据)";
        } else {
            s.tok = RODATA;
            s.desc = "本程序 .rodata";
        }
        return s;
    }
    if (p.find("libc.so") != std::string::npos) {
        if (m.perms.rfind("r-x", 0) == 0) {
            s.tok = LIBC_TEXT;
            s.desc = "libc .text";
        } else {
            s.desc = "libc 段(" + m.perms + ")";
        }
        return s;
    }
    s.desc = p + "(" + m.perms + ")";
    return s;
}

// ---- 被探测的变量们 -------------------------------------------------------
int g_init = 0x42;                         // .data
char g_data_buf[64 * 1024] = {1};          // .data(放大让段可见)
char g_bss_buf[1 << 20];                   // .bss(1MB,必然顶进匿名接续页)
int g_bss_small;                           // .bss(小变量,见下方说明)
const int g_const = 7;                     // .rodata
const char* g_lit = "lm01-rodata-literal"; // 指向 .rodata 的字符串字面量

__attribute__((noinline)) int probe_target(int x) {
    return x + 1;
} // .text
__attribute__((noinline)) int probe_anchor2(int x) {
    return x * 2;
} // .text 第二锚点

struct Probe {
    const void* addr;
    const char* label;  // 标签
    const char* expect; // 预期(人读)
    Tok accept[3];      // 可接受的段 token(OTHER 结尾)
};

// 按显示宽度右补空格(CJK 字符按 2 列计),让表在终端里对齐
std::string pad(const std::string& s, size_t w) {
    size_t col = 0;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = (unsigned char)s[i];
        int bytes = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
        col += (bytes > 1) ? 2 : 1;
        i += (size_t)bytes;
    }
    return s + std::string(col < w ? w - col : 1, ' ');
}

} // namespace

int main() {
    // static 局部与栈、堆、mmap 的样本
    static int s_init = 5;               // .data(只在首次经过时初始化)
    static int s_bss;                    // .bss
    volatile int stack_var = 3;          // [stack]
    volatile char stack_arr[4096];       // [stack]
    stack_arr[0] = 1;                    // 碰一页,确保真分配
    void* p_malloc = std::malloc(64);    // [heap]
    int* p_new = new int(1);             // [heap]
    char* p_new_big = new char[4 << 20]; // 4MB,超 malloc 阈值 → 匿名 mmap
    p_new_big[0] = 1;
    void* p_mmap = ::mmap(nullptr, 2 << 20, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1,
                          0);                                     // mmap 直配
    auto fp_own = reinterpret_cast<unsigned long>(&probe_target); // .text
    auto fp_libc = reinterpret_cast<unsigned long>(&std::printf); // libc .text

    // 中段取样:大 bss 数组必然落在匿名接续页;数组起点可能还在文件映射尾页
    const void* bss_mid = &g_bss_buf[512 * 1024];

    std::printf("== 各类变量地址(全部为同一次运行;ASLR 只影响运行之间) ==\n");
    std::printf("%s = %p\n", pad("g_init(全局已初始化)", 36).c_str(), (void*)&g_init);
    std::printf("%s = %p\n", pad("g_data_buf(64KB 已初始化)", 36).c_str(), (void*)&g_data_buf);
    std::printf("%s = %p\n", pad("&g_bss_buf[0](1MB 未初始化起点)", 36).c_str(), (void*)&g_bss_buf);
    std::printf("%s = %p\n", pad("&g_bss_buf[512K](未初始化中段)", 36).c_str(), bss_mid);
    std::printf("%s = %p\n", pad("g_bss_small(未初始化 int)", 36).c_str(), (void*)&g_bss_small);
    std::printf("%s = %p (值=%d,经指针实读)\n", pad("&g_const(const 常量)", 36).c_str(),
                (void*)&g_const, *(const volatile int*)&g_const);
    std::printf("%s = %p (\"%s\")\n", pad("g_lit(字符串字面量)", 36).c_str(), (const void*)g_lit,
                g_lit);
    std::printf("%s = %p\n", pad("s_init(static 局部已初始化)", 36).c_str(), (void*)&s_init);
    std::printf("%s = %p\n", pad("s_bss(static 局部未初始化)", 36).c_str(), (void*)&s_bss);
    std::printf("%s = %p\n", pad("&stack_var(栈变量)", 36).c_str(), (void*)&stack_var);
    std::printf("%s = %p\n", pad("stack_arr(栈数组)", 36).c_str(), (void*)&stack_arr[0]);
    std::printf("%s = %p\n", pad("malloc(64)", 36).c_str(), p_malloc);
    std::printf("%s = %p\n", pad("new int", 36).c_str(), (void*)p_new);
    std::printf("%s = %p\n", pad("new char[4MB]", 36).c_str(), (void*)p_new_big);
    std::printf("%s = %p\n", pad("mmap(2MB) 直配", 36).c_str(), p_mmap);
    std::printf("%s = %#lx\n", pad("&probe_target(本程序函数)", 36).c_str(), fp_own);
    std::printf("%s = %#lx\n", pad("&probe_anchor2(本程序函数)", 36).c_str(),
                reinterpret_cast<unsigned long>(&probe_anchor2));
    std::printf("%s = %#lx\n", pad("&printf(libc 函数)", 36).c_str(), fp_libc);

    // 读 maps 并逐个对表
    auto maps = read_maps();
    auto find = [&](unsigned long a) -> int {
        for (size_t i = 0; i < maps.size(); ++i)
            if (a >= maps[i].start && a < maps[i].end)
                return (int)i;
        return -1;
    };

    Probe probes[] = {
        {&g_init, "g_init 全局已初始化", ".data", {DATA_RW, OTHER}},
        {&g_data_buf, "g_data_buf 64KB 已初始化", ".data", {DATA_RW, OTHER}},
        {&g_bss_buf,
         "g_bss_buf[0] 1MB 未初始化起点",
         ".bss(起点可能仍在文件 rw 段尾页)",
         {BSS_ANON, DATA_RW, OTHER}},
        {bss_mid, "g_bss_buf[512K] 1MB 未初始化中段", ".bss(匿名接续页)", {BSS_ANON, OTHER}},
        {&g_bss_small,
         "g_bss_small 未初始化 int",
         ".bss(小变量,与 .data 同段混居)",
         {BSS_ANON, DATA_RW, OTHER}},
        {&g_const, "g_const const 常量", ".rodata", {RODATA, OTHER}},
        {g_lit, "字符串字面量", ".rodata", {RODATA, OTHER}},
        {&s_init, "s_init static 局部已初始化", ".data", {DATA_RW, OTHER}},
        {&s_bss,
         "s_bss static 局部未初始化",
         ".bss(与 .data 同段混居)",
         {BSS_ANON, DATA_RW, OTHER}},
        {(const void*)&stack_var, "stack_var 栈变量", "[stack]", {STACK, OTHER}},
        {(const void*)&stack_arr[0], "stack_arr 栈数组", "[stack]", {STACK, OTHER}},
        {p_malloc, "malloc(64)", "[heap]", {HEAP, OTHER}},
        {p_new, "new int", "[heap]", {HEAP, OTHER}},
        {p_new_big, "new char[4MB]", "匿名 mmap(超 128KiB 阈值)", {ANON, OTHER}},
        {p_mmap, "mmap(2MB) 直配", "匿名 mmap", {ANON, OTHER}},
        {(const void*)fp_own, "&probe_target 本程序函数", ".text", {TEXT, OTHER}},
        {(const void*)fp_libc, "&printf libc 函数", "libc .text", {LIBC_TEXT, OTHER}},
        {(const void*)&probe_anchor2, "&probe_anchor2 本程序函数", ".text", {TEXT, OTHER}},
    };

    std::printf("\n== 与 /proc/self/maps 对表(地址 → 落在哪个段 → 与预期比对) ==\n");
    int pass = 0;
    for (const auto& pr : probes) {
        unsigned long a = (unsigned long)pr.addr;
        int idx = find(a);
        std::string verdict = "!!未命中任何映射!!";
        std::string seg = "-";
        if (idx >= 0) {
            SegInfo s = locate(maps, (size_t)idx);
            seg = s.desc;
            bool ok = false;
            for (Tok t : pr.accept)
                if (t == OTHER)
                    break;
                else if (t == s.tok) {
                    ok = true;
                    break;
                }
            verdict = ok ? "一致" : "!!不一致!!";
            if (ok)
                ++pass;
        }
        std::printf("%s %p  %s 预期:%s %s\n", pad(pr.label, 32).c_str(), pr.addr,
                    pad(seg, 42).c_str(), pad(pr.expect, 28).c_str(), verdict.c_str());
    }
    std::printf("对表结果:%zu 项中 %d 项一致\n", sizeof(probes) / sizeof(probes[0]), pass);

    std::printf("\n== 同一次运行的 /proc/self/maps 原文(供逐行核对) ==\n");
    for (const auto& m : maps)
        std::printf("%s\n", m.raw.c_str());
    return 0;
}
