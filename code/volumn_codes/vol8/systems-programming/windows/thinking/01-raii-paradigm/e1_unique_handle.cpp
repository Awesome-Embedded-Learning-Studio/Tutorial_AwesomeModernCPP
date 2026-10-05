// e1_unique_handle.cpp —— unique_handle 骨架 + 失败值矩阵 + 生命周期证明
//
// 编译(WSL 里以相对路径调 MSYS2 UCRT64 g++,cwd 必须在 WSL 文件系统上):
//   /mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -Wall -Wextra e1_unique_handle.cpp -o
//   e1_unique_handle.exe
// 运行(WSL binfmt interop,产物在 WSL 文件系统上要先补执行位):
//   chmod +x e1_unique_handle.exe && ./e1_unique_handle.exe
//
// 观察点:
//   (1) 失败值矩阵:CreateFileW 失败给 INVALID_HANDLE_VALUE(-1),
//       CreateEventW/CreateFileMappingW/OpenProcess 失败给 NULL —— 两套哨兵并存
//   (2) CloseHandle 的哨兵值反应:NULL -> FALSE + err=6;INVALID_HANDLE_VALUE(-1,
//       即 GetCurrentProcess() 伪句柄)-> 静默 TRUE 且不动 last-error;双重关闭 ->
//       FALSE + err=6
//   (3) unique_handle 的 move 全套 + 作用域结束自动关句柄,全程用
//       GetProcessHandleCount 前后计数作证
//   (4) 陷阱演示:unique_handle 直接接管 NULL 失败值时,operator bool 判定错误

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdio>
#include <string>
#include <utility>

// ---- 与本系列 win_util.hpp 同构的 unique_handle(文章 01 的唯一定义处,此处为实验复刻) ----
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

// ---- 观察句柄表计数:任务管理器"句柄"列读的就是它 ----
static DWORD hc() {
    DWORD n = 0;
    if (!GetProcessHandleCount(GetCurrentProcess(), &n)) {
        n = (DWORD)-1;
    }
    return n;
}

static void print_row(const char* api, HANDLE h, DWORD err) {
    printf("  %-22s -> h=%16p  INVALID_HANDLE_VALUE? %-3s  NULL? %-3s  err=%lu\n", api, h,
           h == INVALID_HANDLE_VALUE ? "是" : "否", h == nullptr ? "是" : "否", err);
}

static HANDLE open_ok(const wchar_t* path) {
    return CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                       nullptr);
}

int main() {
    const std::wstring base = L"Local\\sysprog-e1-" + std::to_wstring(GetCurrentProcessId());

    // ---------- (1) 失败值矩阵 ----------
    printf("[1] 失败值矩阵(各 API 打开/创建注定失败的对象)\n");

    HANDLE h1 = CreateFileW(L"no_such_file.bin", GENERIC_READ, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    print_row("CreateFileW", h1, GetLastError());

    HANDLE mutex = CreateMutexW(nullptr, FALSE, (base + L"-collide").c_str());
    HANDLE h2 = CreateEventW(nullptr, FALSE, FALSE, (base + L"-collide").c_str());
    print_row("CreateEventW(撞名)", h2, GetLastError());

    HANDLE h3 = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, 4096,
                                   (base + L"-collide").c_str());
    print_row("CreateFileMappingW(撞名)", h3, GetLastError());

    HANDLE h4 = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, 0xC0FFEEu);
    print_row("OpenProcess(野pid)", h4, GetLastError());

    // 对照:INVALID_HANDLE_VALUE 传给 CreateFileMappingW 不是失败,是"页文件支持"的成功语义
    HANDLE h5 = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, 4096,
                                   (base + L"-mapok").c_str());
    print_row("CreateFileMappingW(ok)", h5, GetLastError());

    // ---------- (2) CloseHandle 的哨兵值反应 + 双重关闭 ----------
    printf("[2] CloseHandle 对哨兵值的反应(先 SetLastError(0) 再观察它写了什么)\n");
    SetLastError(0);
    BOOL r1 = CloseHandle(nullptr);
    DWORD e1v = GetLastError();
    SetLastError(0);
    BOOL r2 = CloseHandle(INVALID_HANDLE_VALUE);
    DWORD e2v = GetLastError();
    SetLastError(0);
    BOOL r3 = CloseHandle(h5); // 第一次关真句柄
    DWORD e3v = GetLastError();
    SetLastError(0);
    BOOL r4 = CloseHandle(h5); // 双重关闭
    DWORD e4v = GetLastError();
    printf("  CloseHandle(NULL)                  -> ret=%d err=%lu\n", r1, e1v);
    printf("  CloseHandle(INVALID_HANDLE_VALUE)  -> ret=%d err=%lu\n", r2, e2v);
    printf("  CloseHandle(有效句柄)第 1 次       -> ret=%d err=%lu\n", r3, e3v);
    printf("  CloseHandle(有效句柄)第 2 次       -> ret=%d err=%lu  (双重关闭)\n", r4, e4v);
    h5 = INVALID_HANDLE_VALUE; // 上面已手动关过,不再用 RAII 接管

    // ---------- (3) unique_handle 生命周期:GetProcessHandleCount 作证 ----------
    printf("[3] unique_handle 生命周期(数字 = 进程当前句柄数)\n");
    DWORD base_cnt = hc();
    printf("  baseline                       : %lu\n", base_cnt);
    {
        unique_handle a{open_ok(L"e1_a.bin")};
        printf("  构造 a(接管 CreateFileW)      : %lu  bool(a)=%d get=%p\n", hc(), (bool)a,
               a.get());
        unique_handle b{std::move(a)};
        printf("  move 构造 b<-a                 : %lu  bool(a)=%d bool(b)=%d\n", hc(), (bool)a,
               (bool)b);
        HANDLE raw = b.release();
        printf("  b.release() 所有权交还         : %lu  bool(b)=%d raw=%p\n", hc(), (bool)b, raw);
        CloseHandle(raw);
        printf("  手动 CloseHandle(raw)          : %lu\n", hc());
        a.reset(open_ok(L"e1_b.bin"));
        printf("  a.reset(新句柄)                : %lu  bool(a)=%d\n", hc(), (bool)a);
        a.reset();
        printf("  a.reset() 立即关闭             : %lu  bool(a)=%d\n", hc(), (bool)a);
        unique_handle c{open_ok(L"e1_c.bin")};
        c = unique_handle{open_ok(L"e1_d.bin")};
        printf("  move 赋值 c<-临时              : %lu\n", hc());
    } // a/b/c 在此析构
    printf("  作用域结束(析构全部执行)     : %lu  (回到 baseline=%lu)\n", hc(), base_cnt);

    // ---------- (4) 陷阱:unique_handle 接管 NULL 失败值 ----------
    printf("[4] 陷阱:把 CreateEventW 的失败值 NULL 直接塞给 unique_handle\n");
    {
        SetLastError(0);
        unique_handle bad{CreateEventW(nullptr, FALSE, FALSE, (base + L"-collide").c_str())};
        printf("  bool(bad)=%d  —— NULL != INVALID_HANDLE_VALUE,-1 哨兵把它判成了\"有效\"\n",
               (bool)bad);
        SetLastError(0);
    } // 析构会对 NULL 调一次 CloseHandle
    printf("  析构里 CloseHandle(NULL) 留下 err=%lu(6=ERROR_INVALID_HANDLE,无害但语义错)\n",
           GetLastError());

    CloseHandle(mutex);
    DeleteFileW(L"e1_a.bin");
    DeleteFileW(L"e1_b.bin");
    DeleteFileW(L"e1_c.bin");
    DeleteFileW(L"e1_d.bin");
    printf("[5] 收尾:mutex 关闭 + 临时文件清理,句柄数=%lu\n", hc());
    return 0;
}
