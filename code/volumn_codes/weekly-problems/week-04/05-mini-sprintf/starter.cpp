#include <climits>
#include <cstdarg>

// ─── 判题辅助(请勿改动):比对两个 NUL 结尾的字符串是否相同 ───

inline bool text_eq(const char* s, const char* t) {
    while (*s != '\0' && *s == *t) {
        ++s;
        ++t;
    }
    return *s == *t;
}

// ─── 您要实现的:迷你 sprintf ───
// 约定:缓冲区 out 足够大,不用考虑截断;返回写入的字符数(不含结尾 '\0')
// 支持四种转换:%d(int)、%s(const char*,NUL 结尾)、%c(char)、%%(输出一个 %)

int mini_sprintf(char* out, const char* fmt, ...) {
    // 在这里写你的实现
}
