// e3_error_code.cpp —— last_error_code() 立即装箱 + 自定义 win32_category + 相等性宇宙
//
// 结论来源(依 e2 实测):
//   - MinGW libstdc++ 的 system_category().message() 给 Win32 文本,但字节是 ANSI 代码页
//     (本机 GBK),UTF-8 终端直读是乱码;FormatMessageW+CP_UTF8 才是正路
//   - 所以本篇两种都做:system_category 版(值语义可靠,文本要自己转码)与
//     win32_category 版(message 直接吐 UTF-8,system_error 的 what() 也跟着可读)
//
// 编译:
//   /mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -Wall -Wextra e3_error_code.cpp -o
//   e3_error_code.exe
// 运行:
//   chmod +x e3_error_code.exe && ./e3_error_code.exe

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdio>
#include <string>
#include <system_error>

// ---- 路线 A:文章 01 的写法,GetLastError 立即装箱进 system_category ----
inline std::error_code last_error_code() noexcept {
    return std::error_code{static_cast<int>(GetLastError()), std::system_category()};
}

// ---- 路线 B:自定义 win32_category,message() 走 FormatMessageW 并转 UTF-8 ----
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
    // 相等性桥接:委托给 libstdc++ 的 system_category(它内置了 win32→errno 映射),
    // 让 error_code(v, win32) 也能和 std::errc 比较
    std::error_condition default_error_condition(int ev) const noexcept override {
        return std::system_category().default_error_condition(ev);
    }
};
inline const win32_category_t& win32_category() {
    static win32_category_t c;
    return c;
}

// ANSI 代码页字节 → UTF-8(给 system_category 的 message 转码用)
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

int main() {
    // ---------- [1] 失败现场立刻装箱 ----------
    printf("[1] CreateFileW(不存在的文件)失败,立刻装箱\n");
    HANDLE h = CreateFileW(L"no_such_file.bin", GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    std::error_code ec = last_error_code(); // 失败分支的头一行就该是它
    printf("    返回值是不是 INVALID_HANDLE_VALUE:%s\n", h == INVALID_HANDLE_VALUE ? "是" : "否");
    printf("    ec.value()=%d  ec.category().name()=%s\n", ec.value(), ec.category().name());
    printf("    ec.message() 原始字节(ANSI 代码页):\"%s\"\n", ec.message().c_str());
    printf("    转码后:\"%s\"\n", acp_to_utf8(ec.message()).c_str());
    printf("    win32_category 渲染同一个值:\"%s\"\n",
           win32_category().message(ec.value()).c_str());

    // ---------- [2] 装箱之后,槽位再怎么折腾都动不了它 ----------
    printf("[2] 装箱后,故意插一次会清零槽位的成功 CreateFileW(e2 实测它成功时置 0)\n");
    HANDLE t = CreateFileW(L"e3_tmp.bin", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    printf("    此时 GetLastError()=%lu(成功调用已把槽位清零)\n", GetLastError());
    printf("    但 ec.value() 仍是 %d —— 装箱即冻结,这就是\"立即\"的意义\n", ec.value());
    CloseHandle(t);
    DeleteFileW(L"e3_tmp.bin");

    // ---------- [3] 相等性的宇宙 ----------
    printf("[3] 相等性:同值不同 category 不相等;errc 桥接走 default_error_condition\n");
    printf("    ec == error_code(2, system_category) : %d  (同 category 同值)\n",
           ec == std::error_code(2, std::system_category()));
    printf("    ec == error_code(2, win32_category)  : %d  (值同,category 不同)\n",
           ec == std::error_code(2, win32_category()));
    printf("    ec == errc::no_such_file_or_directory : %d  (system_category 内置映射桥接)\n",
           ec == std::errc::no_such_file_or_directory);
    std::error_code ec13{13, std::system_category()}; // 合成:13=ERROR_INVALID_DATA
    printf("    合成 error_code(13, system)=\"数据无效。\" == errc::permission_denied : %d\n",
           ec13 == std::errc::permission_denied);
    printf("      (13 在 errno 宇宙才是 EACCES;Win32 13 映射到的是 generic 22/EINVAL)\n");
    auto cond13 = std::system_category().default_error_condition(13);
    printf("      system(13).default_error_condition -> {value=%d, category=%s}\n", cond13.value(),
           cond13.category().name());
    std::error_code w2{2, win32_category()};
    printf(
        "    error_code(2, win32_category) == errc::no_such_file_or_directory : %d  (委托映射)\n",
        w2 == std::errc::no_such_file_or_directory);

    // ---------- [4] 两种装箱怎么选 ----------
    printf("[4] 小结(实测口径)\n");
    printf("    system_category:值语义可靠,libstdc++ 内置 win32->errno 映射可桥接 errc,\n");
    printf("                    但 message() 是 ANSI 代码页字节,得自己转码\n");
    printf("    win32_category :message() 直接 UTF-8,异常的 what() 也跟着可读(见 e4)\n");
    return 0;
}
