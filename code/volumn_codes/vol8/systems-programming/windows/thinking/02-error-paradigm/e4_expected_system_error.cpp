// e4_expected_system_error.cpp —— 双出口约定:工具层 expected,应用顶层 system_error
//
// readFileSize 是"可能失败的查询":返回 std::expected<DWORD64, std::error_code>,
// 失败路径立刻走 last_error_code() 装箱(句柄交给 E1 的 unique_handle 管)。
// main 是应用顶层:失败即致命,把 error_code 包成 std::system_error 抛出并在顶层接住。
//
// 注意:std::expected 是 C++23 组件,本文件用 -std=c++23 编译
//(E1-E3 均为 C++20;本篇是唯一需要 23 的实验)
//
// 编译:
//   /mnt/c/msys64/ucrt64/bin/g++.exe -std=c++23 -Wall -Wextra e4_expected_system_error.cpp -o
//   e4_expected_system_error.exe
// 运行:
//   chmod +x e4_expected_system_error.exe && ./e4_expected_system_error.exe

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdio>
#include <expected>
#include <string>
#include <system_error>
#include <utility>

// ---- E1 的 unique_handle(同款复刻) ----
class unique_handle {
  public:
    explicit unique_handle(HANDLE h = INVALID_HANDLE_VALUE) noexcept : h_(h) {}
    unique_handle(unique_handle&& o) noexcept : h_(std::exchange(o.h_, INVALID_HANDLE_VALUE)) {}
    unique_handle& operator=(unique_handle&& o) noexcept {
        if (this != &o) {
            reset(o.release());
        }
        return *this;
    }
    ~unique_handle() { reset(); }
    HANDLE get() const noexcept { return h_; }
    HANDLE release() noexcept { return std::exchange(h_, INVALID_HANDLE_VALUE); }
    void reset(HANDLE h = INVALID_HANDLE_VALUE) noexcept {
        if (h_ != INVALID_HANDLE_VALUE) {
            ::CloseHandle(h_);
        }
        h_ = h;
    }
    explicit operator bool() const noexcept { return h_ != INVALID_HANDLE_VALUE; }

  private:
    HANDLE h_{INVALID_HANDLE_VALUE};
};

// ---- E3 的 last_error_code 与 win32_category ----
inline std::error_code last_error_code() noexcept {
    return std::error_code{static_cast<int>(GetLastError()), std::system_category()};
}

class win32_category_t : public std::error_category {
  public:
    const char* name() const noexcept override { return "win32"; }
    std::string message(int ev) const override {
        wchar_t* buf = nullptr;
        FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                           FORMAT_MESSAGE_IGNORE_INSERTS,
                       nullptr, (DWORD)ev, 0, reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
        std::wstring ws = buf ? buf : L"(unknown win32 error)";
        LocalFree(buf);
        while (!ws.empty() && (ws.back() == L'\r' || ws.back() == L'\n')) {
            ws.pop_back();
        }
        int n = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), nullptr, 0, nullptr,
                                    nullptr);
        std::string s(size_t(n), '\0');
        WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), s.data(), n, nullptr, nullptr);
        return s;
    }
    std::error_condition default_error_condition(int ev) const noexcept override {
        return std::system_category().default_error_condition(ev);
    }
};
inline const win32_category_t& win32_category() {
    static win32_category_t c;
    return c;
}

static std::string acp_to_utf8(const std::string& a) {
    int w = MultiByteToWideChar(CP_ACP, 0, a.c_str(), (int)a.size(), nullptr, 0);
    std::wstring ws(size_t(w), L'\0');
    MultiByteToWideChar(CP_ACP, 0, a.c_str(), (int)a.size(), ws.data(), w);
    int n =
        WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
    std::string s(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), s.data(), n, nullptr, nullptr);
    return s;
}

// ---- 工具层:可能失败的查询,expected 双出口 ----
std::expected<unsigned long long, std::error_code> read_file_size(const wchar_t* path) {
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return std::unexpected(last_error_code()); // 失败分支头一行:立刻装箱
    }
    unique_handle guard{h}; // 之后所有 return 路径都自动关句柄,这就是 RAII 兜底
    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(guard.get(), &sz)) {
        return std::unexpected(last_error_code());
    }
    return (unsigned long long)sz.QuadPart;
}

int main() {
    // 造一个 123456 字节的测试文件
    {
        unique_handle f{CreateFileW(L"e4_data.bin", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL, nullptr)};
        std::string payload(123456, 'x');
        DWORD w = 0;
        WriteFile(f.get(), payload.data(), (DWORD)payload.size(), &w, nullptr);
    }

    printf("[1] 成功路径:read_file_size(e4_data.bin)\n");
    auto r = read_file_size(L"e4_data.bin");
    printf("    r.has_value()=%d  *r=%llu  (预期 123456)\n", r.has_value(), r.value());

    printf("[2] 失败路径:read_file_size(no_such_file.bin),工具层不带异常\n");
    auto r2 = read_file_size(L"no_such_file.bin");
    printf("    r2.has_value()=%d  r2.error().value()=%d (2=ERROR_FILE_NOT_FOUND)\n",
           r2.has_value(), r2.error().value());
    printf("    system_category 渲染(GBK 转码):\"%s\"\n",
           acp_to_utf8(r2.error().message()).c_str());
    printf("    win32_category 渲染(UTF-8 直读):\"%s\"\n",
           win32_category().message(r2.error().value()).c_str());

    printf("[3] 应用顶层:同一份 error_code 包成 std::system_error 抛出\n");
    try {
        auto s = read_file_size(L"no_such_file.bin");
        if (!s) {
            throw std::system_error{s.error(), "read_file_size"};
        }
        printf("    (不会到这)%llu\n", s.value());
    } catch (const std::system_error& e) {
        printf("    [system_category] e.code().value()=%d\n", e.code().value());
        printf("      what() 原始:  \"%s\"   <-- message 是 ANSI 代码页字节\n", e.what());
        printf("      what() 转码后:\"%s\"\n", acp_to_utf8(e.what()).c_str());
        printf("      e.code() == errc::no_such_file_or_directory : %d\n",
               e.code() == std::errc::no_such_file_or_directory);
    }
    try {
        auto s = read_file_size(L"no_such_file.bin");
        if (!s) {
            throw std::system_error{std::error_code{s.error().value(), win32_category()},
                                    "read_file_size"};
        }
    } catch (const std::system_error& e) {
        printf("    [win32_category] what():\"%s\"  <-- UTF-8 直读,无需转码\n", e.what());
        printf("      e.code() == errc::no_such_file_or_directory : %d\n",
               e.code() == std::errc::no_such_file_or_directory);
    }

    DeleteFileW(L"e4_data.bin");
    return 0;
}
