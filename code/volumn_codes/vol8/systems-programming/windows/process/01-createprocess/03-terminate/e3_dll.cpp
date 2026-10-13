// E3 配套 DLL:DllMain 只用 WriteFile 裸写 stderr(DllMain 里别碰 CRT,加载器锁还攥着)
// 构建: g++.exe -std=c++20 -shared e3_dll.cpp -o e3_dll.dll
#include <windows.h>

static void log_err(const char* tag, const char* msg) {
    HANDLE e = GetStdHandle(STD_ERROR_HANDLE);
    DWORD n;
    if (e != INVALID_HANDLE_VALUE) {
        WriteFile(e, tag, (DWORD)lstrlenA(tag), &n, nullptr);
        WriteFile(e, msg, (DWORD)lstrlenA(msg), &n, nullptr);
    }
}

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            log_err("[dll] ", "DLL_PROCESS_ATTACH (loader notified)\n");
            break;
        case DLL_PROCESS_DETACH:
            log_err("[dll] ", "DLL_PROCESS_DETACH (loader gave us a goodbye chance)\n");
            break;
        default: // 线程级通知保持沉默
            break;
    }
    return TRUE;
}
