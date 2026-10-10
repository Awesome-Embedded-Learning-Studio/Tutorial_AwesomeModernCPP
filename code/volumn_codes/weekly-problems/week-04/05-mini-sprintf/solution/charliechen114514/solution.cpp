#include <cstdarg>

int mini_sprintf(char* out, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char* w = out; // 写游标:下一个字符落在哪
    for (const char* p = fmt; *p != '\0'; ++p) {
        if (*p != '%') { // 普通字符:直接抄
            *w++ = *p;
            continue;
        }
        ++p; // 跳过 %,看转换符
        switch (*p) {
            case 'd': {
                int v = va_arg(args, int);
                unsigned u = (unsigned)v; // INT_MIN 直接取反会溢出,走 unsigned
                if (v < 0) {
                    *w++ = '-';
                    u = 0u - u;
                }
                char tmp[12]; // int 最多 11 位(含负号),倒着存
                int n = 0;
                do { // do-while:值为 0 也要打出那一个 '0'
                    tmp[n++] = (char)('0' + u % 10);
                    u /= 10;
                } while (u != 0);
                while (n > 0)
                    *w++ = tmp[--n]; // 倒序回填
                break;
            }
            case 's': {
                const char* s = va_arg(args, const char*);
                while (*s != '\0')
                    *w++ = *s++;
                break;
            }
            case 'c': // 字符经默认实参提升变成 int,取出来再转回 char
                *w++ = (char)va_arg(args, int);
                break;
            case '%':
                *w++ = '%';
                break;
            default: // 不认识的转换符:原样抄过去(约定里不会出现,防御用)
                *w++ = *p;
                break;
        }
    }
    *w = '\0';
    va_end(args);
    return (int)(w - out); // 写了几个字符,不含结尾 '\0'
}
