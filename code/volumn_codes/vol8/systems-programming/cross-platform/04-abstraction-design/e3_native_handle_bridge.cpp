// e3_native_handle_bridge.cpp —— 篇1 e3:句柄的统一承载与还原(收 ch04/03 留下的口)
//
// cross-platform/03 的 request.target 拿 uintptr_t 糙着装 fd 与 HANDLE,说是
// 留给 ch07 收。本实验就收这个口:
//   1. 两侧各自原生句柄 -> intptr_t -> 原生句柄 的往返是否无损
//   2. sizeof 对拍:fd 是 int(4 字节),HANDLE 是指针(8 字节),intptr_t 两侧都装得下
//   3. Windows 侧走真桥:_open_osfhandle 把 HANDLE 变成 CRT 的 fd,再用 _read 读;
//      反向 _get_osfhandle(fd) 还原出 HANDLE,与原值判等
//   4. 装进 void* 的正确姿势:整数先到 intptr_t 再 reinterpret_cast,直接转编得过但标准不保证往返
//
// 编译口径:
//   Linux  : g++ -std=c++20 -O2 -Wall -Wextra -D_FORTIFY_SOURCE=2
//   Windows: /mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -O2 -Wall -Wextra
#include <cstdint>
#include <cstdio>
#include <cstring>

#ifdef _WIN32
#    define WIN32_LEAN_AND_MEAN
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <fcntl.h> // _O_RDONLY
#    include <io.h>    // _open_osfhandle / _get_osfhandle / _read / _close
#    include <windows.h>
#else
#    include <fcntl.h>
#    include <unistd.h>
#endif

// ---- 统一承载:一个 intptr_t,两侧各自的进出口 ----
class native_handle {
  public:
#ifdef _WIN32
    static native_handle from(HANDLE h) noexcept {
        return native_handle{reinterpret_cast<intptr_t>(h)};
    }
    HANDLE as_handle() const noexcept { return reinterpret_cast<HANDLE>(raw_); }
#else
    static native_handle from(int fd) noexcept { return native_handle{static_cast<intptr_t>(fd)}; }
    int as_fd() const noexcept {
        // int 是 32 位,装进 64 位 intptr_t 时做的是符号扩展;
        // 非负 fd 的高 32 位全零,截断还原无损
        return static_cast<int>(raw_);
    }
#endif
    intptr_t value() const noexcept { return raw_; }

  private:
    explicit native_handle(intptr_t raw) noexcept : raw_(raw) {}
    intptr_t raw_;
};

int main() {
    std::printf("sizeof: int=%zu intptr_t=%zu void*=%zu"
#ifdef _WIN32
                " HANDLE=%zu"
#endif
                "\n",
                sizeof(int), sizeof(std::intptr_t), sizeof(void*)
#ifdef _WIN32
                                                        ,
                sizeof(HANDLE)
#endif
    );

#ifdef _WIN32
    // ---- Windows 侧:HANDLE 往返 + CRT 真桥 ----
    const char* path = "e3_data.txt";
    HANDLE hf = ::CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                              nullptr);
    if (hf == INVALID_HANDLE_VALUE) {
        std::puts("CreateFileA failed");
        return 1;
    }
    const char msg[] = "bridge via _open_osfhandle";
    DWORD written = 0;
    ::WriteFile(hf, msg, sizeof msg - 1, &written, nullptr);
    ::CloseHandle(hf);

    HANDLE h = ::CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        std::puts("open failed");
        return 1;
    }
    std::printf("raw HANDLE        = 0x%llx\n",
                static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(h)));

    // 往返一:HANDLE -> native_handle(intptr_t) -> HANDLE
    native_handle nh = native_handle::from(h);
    HANDLE back = nh.as_handle();
    std::printf("roundtrip HANDLE  = 0x%llx  identical=%s\n",
                static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(back)),
                back == h ? "yes" : "NO");

    // 真桥:_open_osfhandle 给 HANDLE 配一枚 CRT 管理的 fd,_read 就能用了
    int crt_fd = ::_open_osfhandle(reinterpret_cast<intptr_t>(h), _O_RDONLY);
    std::printf("_open_osfhandle   -> crt fd = %d (%s)\n", crt_fd, crt_fd >= 0 ? "ok" : "FAILED");
    char buf[128];
    const int n = ::_read(crt_fd, buf, sizeof buf);
    std::printf("_read(crt fd)     -> %d bytes: '%.*s'\n", n, n > 0 ? n : 0, buf);

    // 反向:_get_osfhandle 从 fd 还原 HANDLE,与原值判等
    HANDLE h2 = reinterpret_cast<HANDLE>(::_get_osfhandle(crt_fd));
    std::printf("_get_osfhandle    -> 0x%llx  identical=%s\n",
                static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(h2)),
                h2 == h ? "yes" : "NO");
    ::_close(crt_fd); // _close 会同时 CloseHandle(所有权在 CRT 一侧后)
    return (back == h && h2 == h && n == static_cast<int>(sizeof msg) - 1) ? 0 : 1;
#else
    // ---- Linux 侧:fd 往返 ----
    const char* path = "e3_data.txt";
    int fd = ::open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        std::puts("open failed");
        return 1;
    }
    const char msg[] = "bridge via intptr_t";
    (void)!::write(fd, msg, sizeof msg - 1);
    ::close(fd);

    fd = ::open(path, O_RDONLY);
    if (fd < 0) {
        std::puts("reopen failed");
        return 1;
    }
    std::printf("raw fd            = %d\n", fd);

    // 往返一:fd -> native_handle(intptr_t) -> fd
    native_handle nh = native_handle::from(fd);
    int back = nh.as_fd();
    std::printf("roundtrip fd      = %d  identical=%s\n", back, back == fd ? "yes" : "NO");

    // 往返二:fd -> void* -> fd。整数不能直接转指针,先到 intptr_t 再 reinterpret
    void* as_ptr = reinterpret_cast<void*>(static_cast<std::intptr_t>(fd));
    int back2 = static_cast<int>(reinterpret_cast<std::intptr_t>(as_ptr));
    std::printf("via void*         = %d  identical=%s\n", back2, back2 == fd ? "yes" : "NO");

    char buf[64];
    const ssize_t n = ::read(back2, buf, sizeof buf); // 还原出的 fd 真的能用
    std::printf("read(roundtripped)= %zd bytes: '%.*s'\n", n, n > 0 ? (int)n : 0, buf);
    ::close(fd);
    return (back == fd && back2 == fd && n == static_cast<ssize_t>(sizeof msg) - 1) ? 0 : 1;
#endif
}
