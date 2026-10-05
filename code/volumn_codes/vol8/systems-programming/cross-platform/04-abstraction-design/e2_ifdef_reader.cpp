// e2_ifdef_reader.cpp —— 篇1 e2 甲场:#ifdef 分发版 file_reader(两侧各编一份)
//
// 同一个函数里用 #ifdef 把两个平台的路都写上。这是最常见的跨平台写法,
// 也是咱们要对照的第一版。数据文件用 std::ofstream 准备(标准库两侧通用),
// 读取走平台原生 API(实验对象)。Windows 侧按子卷口径用 CreateFileW 宽字符版。
//
// 编译口径:
//   Linux  : g++ -std=c++20 -O2 -Wall -Wextra -D_FORTIFY_SOURCE=2
//   Windows: /mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -O2 -Wall -Wextra
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

// ---- #ifdef 分发版:一个函数,两个世界,每个世界一份实现 ----
static std::string read_all_ifdef(const char* path, int& err_out) {
    err_out = 0;
#ifdef _WIN32
    // Windows 世界:句柄 + 宽字符路径 + ReadFile
    wchar_t wpath[512];
    if (::MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, 512) == 0) {
        err_out = 42; // ERROR_INVALID_PARAMETER 的编号,教学骨架里只示意
        return {};
    }
    HANDLE h = ::CreateFileW(wpath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        err_out = static_cast<int>(::GetLastError());
        return {};
    }
    std::string out;
    char buf[4096];
    DWORD got = 0;
    for (;;) {
        if (!::ReadFile(h, buf, sizeof buf, &got, nullptr)) {
            err_out = static_cast<int>(::GetLastError());
            break;
        }
        if (got == 0)
            break;
        out.append(buf, got);
    }
    ::CloseHandle(h);
    return out;
#else
    // POSIX 世界:fd + read
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
    const std::string payload = "hello from #ifdef dispatch\n";

    // 数据准备走标准库:两侧同一份代码
    {
        std::ofstream f(path, std::ios::binary);
        f << payload;
    }

    int err = 0;
    std::string got = read_all_ifdef(path, err);
#ifdef _WIN32
    std::printf("[ifdef] backend=Win32  err=%d len=%zu match=%s\n", err, got.size(),
                got == payload ? "yes" : "NO");
#else
    std::printf("[ifdef] backend=POSIX err=%d len=%zu match=%s\n", err, got.size(),
                got == payload ? "yes" : "NO");
#endif

    // 失败路径:读一个不存在的文件,两侧各自的错误编号原样报告
    int err2 = 0;
    std::string none = read_all_ifdef("e2_no_such_file.txt", err2);
    std::printf(
        "[ifdef] missing-file err=%d len=%zu (POSIX ENOENT=2 / Win32 ERROR_FILE_NOT_FOUND=2)\n",
        err2, none.size());
    return (err == 0 && got == payload) ? 0 : 1;
}
