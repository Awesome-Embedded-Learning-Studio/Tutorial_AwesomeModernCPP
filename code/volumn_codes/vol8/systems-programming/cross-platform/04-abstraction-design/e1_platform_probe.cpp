// e1_platform_probe.cpp —— 篇1 e1:平台检测的前两路(预定义宏 + __has_include)两侧对拍
//
// 三路检测的第 1 路:编译器预定义宏。判定发生在预处理期,程序只是把判定结果
// 打成一张表。宏回答的是"编译器在为哪个目标平台生成代码",回答不了"这台机器
// 的内核有没有某个能力"。
//
// 三路检测的第 2 路:__has_include。它查的是"这个头文件在不在包含路径上",
// 是能力的间接判据:头在,实现多半在;但它查不到版本,也查不到内核。
//
// 编译口径(与本子卷其余篇一致):
//   Linux  : g++ -std=c++20 -O2 -Wall -Wextra -D_FORTIFY_SOURCE=2
//   Windows: /mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -O2 -Wall -Wextra
#include <cstdio>

#ifdef _WIN32
#    define WIN32_LEAN_AND_MEAN
// MSYS2 的 libstdc++ 在 os_defines.h 里已经替咱们定义了 NOMINMAX(实测 GCC 16.1),
// 再定义一次会触发 -Wall 的 redefined 警告,所以加一层守卫。
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <windows.h> // GetSystemInfo:运行期才知道的信息(Windows 侧对拍用)
#else
#    include <sys/utsname.h> // uname:运行期才知道的信息,拿来与编译期宏对拍
#endif

// ---- 第 1 路:预定义宏普查。每行都是一条独立的预处理判定 ----
struct macro_row {
    const char* name;
    bool on;
    const char* note;
};

static const macro_row kMacros[] = {
#ifdef __linux
    {"__linux", true, "目标平台为 Linux(编译器声明)"},
#else
    {"__linux", false, ""},
#endif
#ifdef __linux__
    {"__linux__", true, "同上的带尾缀版本(gcc 历史习惯)"},
#else
    {"__linux__", false, ""},
#endif
#ifdef __GLIBC__
    {"__GLIBC__", true, "C 库是 glibc(版本见 __GLIBC_MINOR__)"},
#else
    {"__GLIBC__", false, "MSYS2 用 mingw-w64 crt,不是 glibc"},
#endif
#ifdef _UCRT
    {"_UCRT", true, "MSYS2 UCRT64 环境定义,Universal CRT"},
#else
    {"_UCRT", false, ""},
#endif
#ifdef _WIN32
    {"_WIN32", true, "Win32/Win64 都定义(目标为 Windows)"},
#else
    {"_WIN32", false, ""},
#endif
#ifdef _WIN64
    {"_WIN64", true, "仅 64 位 Windows 目标"},
#else
    {"_WIN64", false, ""},
#endif
#ifdef __MINGW64__
    {"__MINGW64__", true, "MinGW-w64 工具链"},
#else
    {"__MINGW64__", false, ""},
#endif
#ifdef __CYGWIN__
    {"__CYGWIN__", true, "Cygwin 环境(POSIX 兼容层)"},
#else
    {"__CYGWIN__", false, ""},
#endif
#ifdef __APPLE__
    {"__APPLE__", true, "macOS 目标"},
#else
    {"__APPLE__", false, "两侧都不该出现,作对照"},
#endif
#ifdef __FreeBSD__
    {"__FreeBSD__", true, "FreeBSD 目标"},
#else
    {"__FreeBSD__", false, "kqueue 的家,本机没有"},
#endif
};

// ---- 第 2 路:__has_include 能力探头(以四个异步/通知机制的头为样本) ----
struct probe_row {
    const char* header;
    bool present;
    const char* note;
};

static const probe_row kHeaders[] = {
#if defined(__has_include)
#    if __has_include(<sys/epoll.h>)
    {"<sys/epoll.h>", true, "epoll 用户态接口(glibc 提供,内核 2.6 起才有机制)"},
#    else
    {"<sys/epoll.h>", false, ""},
#    endif
#    if __has_include(<sys/event.h>)
    {"<sys/event.h>", true, "kqueue(BSD/macOS)"},
#    else
    {"<sys/event.h>", false, "Linux 上没有 kqueue"},
#    endif
#    if __has_include(<liburing.h>)
    {"<liburing.h>", true, "io_uring 的辅助库(独立安装,非内核附属)"},
#    else
    {"<liburing.h>", false, ""},
#    endif
#    if __has_include(<windows.h>)
    {"<windows.h>", true, "Win32 全家"},
#    else
    {"<windows.h>", false, ""},
#    endif
#    if __has_include(<sys/inotify.h>)
    {"<sys/inotify.h>", true, "inotify(Linux 独有)"},
#    else
    {"<sys/inotify.h>", false, ""},
#    endif
#else
    {"(no __has_include)", false, "编译器不支持,探头整体失效"},
#endif
};

int main() {
    std::printf("== route 1: predefined macros (compile-time, per target) ==\n");
    for (const auto& m : kMacros)
        std::printf("  %-14s %-8s %s\n", m.name, m.on ? "DEFINED" : "absent", m.note);

    std::printf("== route 2: __has_include probes (compile-time, per toolchain) ==\n");
    for (const auto& h : kHeaders)
        std::printf("  %-18s %-8s %s\n", h.header, h.present ? "present" : "ABSENT", h.note);

    // ---- 运行期对拍:这层信息宏永远拿不到 ----
    // Linux 用 uname(2),Windows 用 GetSystemInfo。这里两行 #ifdef 分流实现的
    // 用法,正是预处理该待的位置:贴着平台的边界,而不是铺满业务逻辑。
#ifdef _WIN32
    std::printf("== runtime: GetSystemInfo (information macros can never carry) ==\n");
    SYSTEM_INFO si{};
    ::GetSystemInfo(&si);
    std::printf("  processor_architecture=%u number_of_processors=%lu\n",
                static_cast<unsigned>(si.wProcessorArchitecture),
                static_cast<unsigned long>(si.dwNumberOfProcessors));
#else
    std::printf("== runtime: uname(2) (information macros can never carry) ==\n");
    struct utsname u{};
    if (::uname(&u) == 0) {
        std::printf("  sysname=%s release=%s machine=%s\n", u.sysname, u.release, u.machine);
    }
#endif
    return 0;
}
