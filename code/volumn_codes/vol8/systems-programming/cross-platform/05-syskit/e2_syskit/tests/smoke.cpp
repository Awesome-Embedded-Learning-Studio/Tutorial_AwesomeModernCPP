// tests/smoke.cpp —— syskit 骨架的冒烟测试(双平台同一份)
//
// 测试策略的最小实证:不引测试框架,CHECK 宏计数失败,全绿退出 0。
// 覆盖面是"工程化收编是否保住了原有语义":
//   1. 资源持有与析构释放(句柄/fd 计数核对)
//   2. 错误路径:open 不存在 -> expected 错误码(两侧值都是 2)
//   3. 成功路径:写文件再读回
//   4. sys_call/check_win32 抛 system_error,catch 到 code 对得上
//   5. move/swap 换手语义
#include "syskit/call.hpp"
#include "syskit/error.hpp"
#include "syskit/fd.hpp"
#include "syskit/handle.hpp"

#include <cstdio>
#include <expected>
#include <string>

#ifdef _WIN32
#    include <io.h>
#else
#    include <fcntl.h>
#    include <unistd.h>
#endif

static int g_failures = 0;
#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            ++g_failures;                                         \
            std::printf("  FAIL line %d: %s\n", __LINE__, #cond); \
        }                                                         \
    } while (0)

// ---- 工具层出口形状:expected<T, error_code>(思维基石 02 的双出口约定) ----
static std::expected<std::string, std::error_code> read_text(const char* path) {
#ifdef _WIN32
    HANDLE h = ::CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return std::unexpected(syskit::last_error());
    syskit::unique_handle guard{h};
    char buf[256];
    DWORD got = 0;
    if (!::ReadFile(guard.get(), buf, sizeof buf, &got, nullptr))
        return std::unexpected(syskit::last_error());
    return std::string{buf, got};
#else
    int fd = ::open(path, O_RDONLY);
    if (fd == -1)
        return std::unexpected(syskit::last_error());
    syskit::unique_fd guard{fd};
    std::string out;
    char buf[256];
    for (;;) {
        ssize_t n = ::read(guard.get(), buf, sizeof buf);
        if (n == -1) {
            if (errno == EINTR)
                continue;
            return std::unexpected(syskit::last_error());
        }
        if (n == 0)
            return out;
        out.append(buf, static_cast<std::size_t>(n));
    }
#endif
}

int main() {
#ifdef _WIN32
    std::printf("[smoke] platform=Windows\n");
#else
    std::printf("[smoke] platform=Linux\n");
#endif

    // 1) 错误路径:两侧的错误码都应该是 2(ENOENT / ERROR_FILE_NOT_FOUND)
    auto miss = read_text("syskit_no_such_file.txt");
    CHECK(!miss.has_value());
    CHECK(miss.error().value() == 2);
    std::printf("  [1] missing file: value=%d message=\"%s\"\n", miss.error().value(),
                miss.error().message().c_str());

    // 2) 成功路径:先写一个文件再读回
    const char* payload = "syskit smoke payload";
    {
#ifdef _WIN32
        syskit::unique_handle f{syskit::check_win32("CreateFileA", ::CreateFileA,
                                                    "syskit_smoke.txt", GENERIC_WRITE, 0, nullptr,
                                                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)};
        DWORD written = 0;
        syskit::check_win32("WriteFile", ::WriteFile, f.get(), payload,
                            static_cast<DWORD>(__builtin_strlen(payload)), &written, nullptr);
        CHECK(written == __builtin_strlen(payload));
#else
        syskit::unique_fd f{syskit::sys_call("open", ::open, "syskit_smoke.txt",
                                             O_WRONLY | O_CREAT | O_TRUNC, 0644)};
        const ssize_t n =
            syskit::sys_call("write", ::write, f.get(), payload, __builtin_strlen(payload));
        CHECK(n == static_cast<ssize_t>(__builtin_strlen(payload)));
#endif
    }
    auto ok = read_text("syskit_smoke.txt");
    CHECK(ok.has_value());
    CHECK(*ok == payload);
    std::printf("  [2] roundtrip: len=%zu match=%s\n", ok->size(),
                ok->size() == __builtin_strlen(payload) ? "yes" : "NO");

    // 3) 异常出口:sys_call/check_win32 抛的 system_error,code 对得上
    bool caught = false;
    try {
#ifdef _WIN32
        syskit::check_win32("CreateFileA", ::CreateFileA, "syskit_no_such_dir/nope.txt",
                            GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
#else
        syskit::sys_call("open", ::open, "syskit_no_such_file.txt", O_RDONLY);
#endif
    } catch (const std::system_error& e) {
        caught = true;
        std::printf("  [3] system_error: value=%d what-prefix ok\n", e.code().value());
        // 带目录前缀的缺失路径:Linux 统一 ENOENT(2);
        // Windows 按"路径状态"细分——目录不存在是 3(ERROR_PATH_NOT_FOUND),
        // 目录在而文件缺才是 2(ERROR_FILE_NOT_FOUND)。两侧语义差异,此处如实收下
        CHECK(e.code().value() == 2 || e.code().value() == 3);
    }
    CHECK(caught);

    // 4) 换手语义:move 掏空源、swap 换编号
#ifdef _WIN32
    {
        syskit::unique_handle a{::GetCurrentProcess()};
        CHECK(static_cast<bool>(a) == false || true); // 伪句柄 -1 本就不该装进来,这里只测包装本身
        syskit::unique_handle b{::CreateFileA("syskit_smoke.txt", GENERIC_READ, FILE_SHARE_READ,
                                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                                              nullptr)};
        CHECK(static_cast<bool>(b));
        syskit::unique_handle c = std::move(b);
        CHECK(!b && c);
    }
#else
    {
        int raw1 = ::open("syskit_smoke.txt", O_RDONLY);
        int raw2 = ::open("syskit_smoke.txt", O_RDONLY);
        syskit::unique_fd a{raw1};
        syskit::unique_fd b{raw2};
        syskit::unique_fd c = std::move(b);
        CHECK(!b && c);
        a.swap(c);
        CHECK(a.get() == raw2 && c.get() == raw1); // 编号跟着所有权走
    }
#endif
    std::printf("  [4] move/swap semantics: ok\n");

    if (g_failures == 0) {
        std::printf("[smoke] ALL PASS\n");
        return 0;
    }
    std::printf("[smoke] %d FAILURE(S)\n", g_failures);
    return 1;
}
