// e5:MinGW 的 C++ throw 走不走 SEH 码路径?
// 探针:VEH 全程在岗,数每一次异常分发的 code。
// 预期(x64 MSVC 的事实):throw → _CxxThrowException → RtlRaiseException,
// 异常码 0xE06D7363('msc'+0xE0),先过 VEH。MinGW-w64 x64 的 EH 也是 SEH 实现
// (.pdata/.xdata 表 + __gxx_personality_seh0),所以应当同样过 VEH。
#define WIN32_LEAN_AND_MEAN
#include <cstdio>
#include <stdexcept>
#include <windows.h>

static int dispatch_count = 0;

static LONG WINAPI counting_veh(PEXCEPTION_POINTERS ep) {
    ++dispatch_count;
    PEXCEPTION_RECORD r = ep->ExceptionRecord;
    printf("  [VEH dispatch #%d] code=0x%08lX\n", dispatch_count, (unsigned long)r->ExceptionCode);
    if (r->ExceptionCode == 0xE06D7363)
        printf("      ^ 0xE06D7363 = MSC EH(C++ throw 的码,'msc' ASCII + 0xE0000000)\n");
    return EXCEPTION_CONTINUE_SEARCH; // 只观察,不拦截
}

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    AddVectoredExceptionHandler(1, counting_veh);
    printf("VEH in place. dispatch_count=%d\n", dispatch_count);

    printf("== throw std::runtime_error,外面有 catch ==\n");
    try {
        throw std::runtime_error("from e5");
    } catch (const std::exception& e) {
        printf("  caught: %s (dispatch_count=%d,应为 1)\n", e.what(), dispatch_count);
    }

    printf("== throw 一个 int ==\n");
    try {
        throw 42;
    } catch (int v) {
        printf("  caught int %d (dispatch_count=%d,还是 1 —— catch 不再产生异常分发)\n", v,
               dispatch_count);
    }

    printf("== 再 throw 一次(计数应到 2,每次 throw 恰好一次分发)==\n");
    try {
        throw std::runtime_error("second");
    } catch (const std::exception& e) {
        printf("  caught: %s (dispatch_count=%d)\n", e.what(), dispatch_count);
    }

    printf("done\n");
    return 0;
}
