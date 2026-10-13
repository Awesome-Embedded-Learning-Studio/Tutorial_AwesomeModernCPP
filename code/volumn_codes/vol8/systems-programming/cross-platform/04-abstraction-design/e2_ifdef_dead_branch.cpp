// e2_ifdef_dead_branch.cpp —— 篇1 e2 乙场:被裁掉的分支里藏错,当前平台零诊断
//
// 与甲场同一个骨架,只改一件事:Windows 分支里藏了三处错误——
//   1. ReaddFile(拼错函数名)
//   2. DWORDD(拼错类型)
//   3. ::CloseHandle(h, 2)(参数个数错)
// 预处理器在 Linux 上直接把这个分支整个扔掉,三处错误一处都不会报;
// 同一份文件拿到 Windows 侧编译,三处全部现形。
// 这就是"#ifdef 裁掉的代码不编译"的实测口径:死分支里的错误可以沉睡任意久,
// 直到有人把这份代码带到(或交叉编译到)那个平台。
//
// 编译口径:同甲场。Linux 侧预期零警告通过;Windows 侧预期编译失败(诊断入档)。
#include <cstdio>
#include <fstream>
#include <string>

#ifdef _WIN32
#    define WIN32_LEAN_AND_MEAN
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <windows.h>
#else
#    include <fcntl.h>
#    include <unistd.h>
#endif

static std::string read_all_dead(const char* path, int& err_out) {
    err_out = 0;
#ifdef _WIN32
    wchar_t wpath[512];
    ::MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, 512);
    HANDLE h = ::CreateFileW(wpath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        err_out = static_cast<int>(::GetLastError());
        return {};
    }
    std::string out;
    char buf[4096];
    DWORDD got = 0; // 错误 2:类型拼错,应为 DWORD
    for (;;) {
        if (!ReaddFile(h, buf, sizeof buf, &got, nullptr)) { // 错误 1:函数拼错
            err_out = static_cast<int>(::GetLastError());
            break;
        }
        if (got == 0)
            break;
        out.append(buf, got);
    }
    ::CloseHandle(h, 2); // 错误 3:参数个数错
    return out;
#else
    int fd = ::open(path, O_RDONLY);
    if (fd == -1) {
        err_out = errno;
        return {};
    }
    std::string out;
    char buf[4096];
    for (;;) {
        ssize_t n = ::read(fd, buf, sizeof buf);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            err_out = errno;
            break;
        }
        if (n == 0)
            break;
        out.append(buf, static_cast<std::size_t>(n));
    }
    ::close(fd);
    return out;
#endif
}

int main(int argc, char** argv) {
    const char* path = (argc > 1) ? argv[1] : "e2_data.txt";
    const std::string payload = "hello from dead branch\n";
    {
        std::ofstream f(path, std::ios::binary);
        f << payload;
    }
    int err = 0;
    std::string got = read_all_dead(path, err);
    std::printf(
        "[dead-branch] err=%d len=%zu match=%s -- 编译时死分支里的三处错误,本平台一声不吭\n", err,
        got.size(), got == payload ? "yes" : "NO");
    return 0;
}
