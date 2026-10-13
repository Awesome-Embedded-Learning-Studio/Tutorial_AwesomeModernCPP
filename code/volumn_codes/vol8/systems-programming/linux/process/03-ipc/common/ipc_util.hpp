// common/ipc_util.hpp —— 《IPC:管道、FIFO 与 POSIX 消息队列》实验的公共工具
//
// errno_code / sys_call 与思维基石(thinking/01-raii-paradigm/common/raii.hpp)
// 同款契约:错误统一走 std::system_error;此外提供单调时钟毫秒、
// /proc/self/fdinfo 的 pos 读取(E6 用)、fd 表清点(E2 用)。
//
// 编译口径:g++ -std=c++20 -Wall -Wextra -O2 -I common,实测 g++ 16.2.1 零警告。

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <system_error>

#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

// 契约一:把 errno 翻成 error_code(与思维基石同款)
inline std::error_code errno_code() {
    return {errno, std::generic_category()};
}

// 契约二:任何「返回 -1 表失败」的 syscall 都从这儿过
template <class F, class... Args> auto sys_call(const char* what, F&& f, Args&&... args) {
    auto result = std::forward<F>(f)(std::forward<Args>(args)...);
    if (result == -1) {
        throw std::system_error{errno, std::generic_category(), what};
    }
    return result;
}

// 单调时钟:进程启动起的毫秒数(时序输出统一用它)
inline double ms_since(const std::chrono::steady_clock::time_point& t0) {
    auto d = std::chrono::steady_clock::now() - t0;
    return std::chrono::duration<double, std::milli>(d).count();
}

// 读 /proc/self/fdinfo/<fd> 的 pos 字段(内核记的文件偏移,E6 的客观证人)
inline long fd_pos(int fd) {
    char path[64];
    std::snprintf(path, sizeof path, "/proc/self/fdinfo/%d", fd);
    std::FILE* f = std::fopen(path, "r");
    if (!f)
        return -1;
    long pos = -1;
    char line[128];
    while (std::fgets(line, sizeof line, f)) {
        if (std::sscanf(line, "pos:%ld", &pos) == 1)
            break;
    }
    std::fclose(f);
    return pos;
}

// 数一遍 /proc/self/fd 里的目录项(E2 观察泄漏/继承用)
inline int fd_count() {
    int count = 0;
    DIR* d = opendir("/proc/self/fd");
    if (!d)
        return -1;
    while (readdir(d) != nullptr)
        ++count;
    closedir(d);
    return count - 2; // 减掉 "." 和 ".."
}
