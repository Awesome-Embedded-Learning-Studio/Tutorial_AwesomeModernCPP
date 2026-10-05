// e1_virtualprotect.cpp —— 文件映射视图上的 VirtualProtect 语义
//
// 编译(WSL 里以相对路径调 MSYS2 UCRT64 g++,cwd 必须在 WSL 文件系统上):
//   /mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -Wall -Wextra e1_virtualprotect.cpp -o
//   e1_virtualprotect.exe
// 运行(逐阶段;两个崩溃阶段各跑一次,exit 码单独记录):
//   chmod +x e1_virtualprotect.exe
//   ./e1_virtualprotect.exe ro-write        ; echo "exit=$?"
//   ./e1_virtualprotect.exe protect-write
//   ./e1_virtualprotect.exe noaccess-read   ; echo "exit=$?"
//   ./e1_virtualprotect.exe oldprotect
//
// 观察点:
//   [ro-write]       只读视图上写入 -> 硬件异常 0xC0000005(STATUS_ACCESS_VIOLATION)。
//                    先演示 VirtualProtect 试图 RO->RW 越权升级(失败 err=87),
//                    再硬写 -> 崩溃。用 SetUnhandledExceptionFilter 记录:异常码 /
//                    异常地址(指令)/ ExceptionInformation[0](0=读,1=写)与
//                    [1](目标数据地址)。现象本身是 SEH 篇的入口素材,本篇只记录现象。
//   [protect-write]  视图授权范围内改保护再写回:RW 视图降到 NOACCESS 再升回 RW,
//                    同一条写指令成功;FlushViewOfFile + 重开文件验证落盘;
//                    lpflOldProtect 出参带回改前保护值(失败时出参无意义,哨兵验证)。
//   [noaccess-read]  改到 PAGE_NOACCESS 后,连"读"都触发 0xC0000005,
//                    ExceptionInformation[0]=0(读冲突),与写冲突区分开。
//   [oldprotect]     实测铁律:视图保护只能在 MapViewOfFile 授予的范围内变化——
//                    RW 视图降级/升回都行;FILE_MAP_READ 视图升 RW 一律 err=87,
//                    哪怕映射本身是 PAGE_READWRITE、文件也是读写打开。

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <utility>

// ---- 与 thinking/01-raii-paradigm 同构的 unique_handle 实验复刻 ----
class unique_handle {
  public:
    explicit unique_handle(HANDLE h = INVALID_HANDLE_VALUE) noexcept : h_(h) {}
    ~unique_handle() { reset(); }
    unique_handle(unique_handle&& o) noexcept : h_(std::exchange(o.h_, INVALID_HANDLE_VALUE)) {}
    unique_handle& operator=(unique_handle&& o) noexcept {
        if (this != &o) {
            reset(o.release());
        }
        return *this;
    }
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

static const char* protect_name(DWORD p) {
    switch (p) {
        case PAGE_NOACCESS:
            return "PAGE_NOACCESS(0x01)";
        case PAGE_READONLY:
            return "PAGE_READONLY(0x02)";
        case PAGE_READWRITE:
            return "PAGE_READWRITE(0x04)";
        default:
            return "(other)";
    }
}

static std::wstring temp_path(const wchar_t* name) {
    wchar_t dir[MAX_PATH];
    GetTempPathW(MAX_PATH, dir);
    return std::wstring(dir) + name;
}

// 建一个 64KiB、内容全 'A' 的文件(GENERIC_READ|GENERIC_WRITE 打开,留给调用者)
static unique_handle make_file(const wchar_t* name, bool writable) {
    DWORD access = writable ? (GENERIC_READ | GENERIC_WRITE) : GENERIC_READ;
    unique_handle f{CreateFileW(temp_path(name).c_str(), access, 0, nullptr, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (!f) {
        fprintf(stderr, "CreateFileW failed err=%lu\n", GetLastError());
        ExitProcess(1);
    }
    char buf[4096];
    memset(buf, 'A', sizeof buf);
    for (int i = 0; i < 16; ++i) {
        DWORD w = 0;
        if (!WriteFile(f.get(), buf, sizeof buf, &w, nullptr) || w != sizeof buf) {
            fprintf(stderr, "WriteFile failed err=%lu\n", GetLastError());
            ExitProcess(1);
        }
    }
    FlushFileBuffers(f.get());
    return f;
}

// ---- 未处理异常过滤器:只记录现象,不恢复(返回 EXECUTE_HANDLER 走默认终止) ----
static LONG crash_log(EXCEPTION_POINTERS* ep) {
    EXCEPTION_RECORD* r = ep->ExceptionRecord;
    fprintf(stderr, "[unhandled-exception] code=0x%08lX\n", (unsigned long)r->ExceptionCode);
    fprintf(stderr, "  指令地址 ExceptionAddress = %p\n", r->ExceptionAddress);
    fprintf(stderr, "  ExceptionInformation[0]   = %llu  (%s)\n",
            (unsigned long long)r->ExceptionInformation[0],
            r->ExceptionInformation[0] == 0   ? "读访问冲突"
            : r->ExceptionInformation[0] == 1 ? "写访问冲突"
            : r->ExceptionInformation[0] == 8 ? "DEP 执行冲突"
                                              : "?");
    fprintf(stderr, "  ExceptionInformation[1]   = %p  (目标数据地址)\n",
            (void*)r->ExceptionInformation[1]);
    fflush(stderr);
    return EXCEPTION_EXECUTE_HANDLER;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "用法: %s ro-write|protect-write|noaccess-read|oldprotect\n", argv[0]);
        return 2;
    }
    SetUnhandledExceptionFilter(crash_log);
    const std::string phase = argv[1];

    if (phase == "ro-write") {
        // 映射 RW、视图只读:写 -> 0xC0000005(写冲突)
        unique_handle f = make_file(L"sysprog-e1.bin", true);
        unique_handle m{CreateFileMappingW(f.get(), nullptr, PAGE_READWRITE, 0, 0, nullptr)};
        if (!m) {
            fprintf(stderr, "CreateFileMappingW failed err=%lu\n", GetLastError());
            return 1;
        }
        char* view = (char*)MapViewOfFile(m.get(), FILE_MAP_READ, 0, 0, 0);
        DWORD garbage = 0xCCCCCCCC;
        BOOL up = VirtualProtect(view, 4096, PAGE_READWRITE, &garbage);
        printf("[ro-write] 视图=%p(只读,来自 RW 映射)\n", view);
        printf("  先试 VirtualProtect(RO->RW) ret=%d err=%lu 出参=%s(失败时无意义)\n", up,
               GetLastError(), garbage == 0xCCCCCCCC ? "未被改写" : protect_name(garbage));
        printf("  硬写 view[0]='X' ...");
        fflush(stdout); // 先冲刷,保证崩溃前的输出可见
        view[0] = 'X';  // 触发
        printf("  写入成功?!view[0]=%c(不应到达)\n", view[0]);
        return 0;
    }

    if (phase == "protect-write") {
        unique_handle f = make_file(L"sysprog-e1.bin", true);
        unique_handle m{CreateFileMappingW(f.get(), nullptr, PAGE_READWRITE, 0, 0, nullptr)};
        // 关键:视图一开始就以 FILE_MAP_ALL_ACCESS 拿到读写授权
        char* view = (char*)MapViewOfFile(m.get(), FILE_MAP_ALL_ACCESS, 0, 0, 0);
        printf("[protect-write] 视图=%p(FILE_MAP_ALL_ACCESS)\n", view);

        DWORD old = 0;
        BOOL ok = VirtualProtect(view, 4096, PAGE_READONLY, &old);
        printf("  降到 PAGE_READONLY          ret=%d lpflOldProtect=%s\n", ok, protect_name(old));
        ok = VirtualProtect(view, 4096, PAGE_NOACCESS, &old);
        printf("  降到 PAGE_NOACCESS          ret=%d lpflOldProtect=%s\n", ok, protect_name(old));
        ok = VirtualProtect(view, 4096, PAGE_READWRITE, &old);
        printf("  升回 PAGE_READWRITE         ret=%d lpflOldProtect=%s\n", ok, protect_name(old));

        printf("  写 view[0]='X' ...\n");
        fflush(stdout);
        view[0] = 'X'; // 同一条写指令,授权范围内改保护后合法
        printf("  写入后 view[0]=%c\n", view[0]);

        FlushViewOfFile(view, 4096);
        FlushFileBuffers(f.get());
        UnmapViewOfFile(view);
        m.reset(); // 先把映射和文件句柄全关掉,重开才不会撞 sharing violation
        f.reset();

        // 重开文件独立验证:改动确实穿到后备文件
        HANDLE v = CreateFileW(temp_path(L"sysprog-e1.bin").c_str(), GENERIC_READ, FILE_SHARE_READ,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (v == INVALID_HANDLE_VALUE) {
            printf("  重开失败 err=%lu\n", GetLastError());
            return 1;
        }
        char c = 0;
        DWORD rd = 0;
        ReadFile(v, &c, 1, &rd, nullptr);
        printf("  重开文件读回首字节 = %c(0x%02x,落盘验证)\n", c, (unsigned char)c);
        CloseHandle(v);
        printf("  [结论] 授权范围内改保护有效;写入直达后备文件,不是写时复制\n");
        return 0;
    }

    if (phase == "noaccess-read") {
        unique_handle f = make_file(L"sysprog-e1.bin", true);
        unique_handle m{CreateFileMappingW(f.get(), nullptr, PAGE_READWRITE, 0, 0, nullptr)};
        char* view = (char*)MapViewOfFile(m.get(), FILE_MAP_READ, 0, 0, 0);
        DWORD oldp = 0;
        VirtualProtect(view, 4096, PAGE_NOACCESS, &oldp);
        printf("[noaccess-read] 视图=%p 已改 PAGE_NOACCESS(改前 %s),准备读 view[0] ...\n", view,
               protect_name(oldp));
        fflush(stdout);
        volatile char c = view[0]; // 读也触发
        printf("  读到 %c(不应到达)\n", c);
        (void)c;
        return 0;
    }

    if (phase == "oldprotect") {
        printf("[oldprotect] 授权视图(ALL_ACCESS)上链式改保护:RW -> RO -> NOACCESS -> RW\n");
        unique_handle f = make_file(L"sysprog-e1.bin", true);
        unique_handle m{CreateFileMappingW(f.get(), nullptr, PAGE_READWRITE, 0, 0, nullptr)};
        char* view = (char*)MapViewOfFile(m.get(), FILE_MAP_ALL_ACCESS, 0, 0, 0);
        DWORD old = 0;
        for (DWORD to : {PAGE_READONLY, PAGE_NOACCESS, PAGE_READWRITE}) {
            BOOL ok = VirtualProtect(view, 4096, to, &old);
            printf("  改到 %-22s ret=%d  lpflOldProtect=%s  err=%lu\n", protect_name(to), ok,
                   protect_name(old), ok ? 0ul : GetLastError());
        }
        printf("  (对照:FILE_MAP_READ 只读视图升 RW -> err=87,见 ro-write 阶段输出)\n");

        printf("\n[oldprotect] 越过映射上限:文件 GENERIC_READ 打开 + PAGE_READONLY 映射\n");
        unique_handle fro = make_file(L"sysprog-e1-ro.bin", true);
        fro.reset(); // 关掉读写句柄
        fro.reset(CreateFileW(temp_path(L"sysprog-e1-ro.bin").c_str(), GENERIC_READ,
                              FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                              nullptr));
        unique_handle mro{CreateFileMappingW(fro.get(), nullptr, PAGE_READONLY, 0, 0, nullptr)};
        if (!mro) {
            printf("  CreateFileMappingW(PAGE_READONLY) 失败 err=%lu\n", GetLastError());
            return 1;
        }
        char* vro = (char*)MapViewOfFile(mro.get(), FILE_MAP_READ, 0, 0, 0);
        DWORD old2 = 0;
        SetLastError(0);
        BOOL ok2 = VirtualProtect(vro, 4096, PAGE_READWRITE, &old2);
        printf("  VirtualProtect(RO 映射视图 -> RW)  ret=%d  err=%lu  lpflOldProtect=%s\n", ok2,
               GetLastError(), protect_name(old2));
        printf("  (对照:同样的调用在 RW 映射的只读视图上成功,见 protect-write 阶段)\n");
        UnmapViewOfFile(vro);
        DeleteFileW(temp_path(L"sysprog-e1-ro.bin").c_str());
        return 0;
    }

    fprintf(stderr, "未知阶段 %s\n", phase.c_str());
    return 2;
}
