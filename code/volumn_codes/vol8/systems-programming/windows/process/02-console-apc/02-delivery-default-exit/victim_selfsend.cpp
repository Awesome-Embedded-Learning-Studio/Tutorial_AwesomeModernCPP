// cmd ERRORLEVEL 口径交叉验证:无 handler + 复位忽略位 + 自己给自己发 CTRL_C → 默认 handler
// ExitProcess
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
int main() {
    SetConsoleCtrlHandler(NULL, FALSE);        // 清 WSL interop 继承的忽略位
    GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0); // 发给本组(自己)
    return 42;                                 // 走到这说明默认 handler 没杀我(不应发生)
}
