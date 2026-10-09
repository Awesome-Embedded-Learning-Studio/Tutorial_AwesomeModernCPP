// sigout.hpp —— 信号 handler 上下文唯一允许的输出方式:裸 write。
// 与文章 sigbus 节选的 write_all/write_hex 同款;十六进制手工逐位生成,
// 不碰 printf/snprintf(它们不异步信号安全)。
#pragma once

#include <cstddef>
#include <cstdint>

#include <unistd.h>

inline void write_all(const char* s, std::size_t n)
{
    while (n > 0) {
        ssize_t w = ::write(STDERR_FILENO, s, n); // handler 里只用 write
        if (w <= 0) {
            return;
        }
        s += w;
        n -= static_cast<std::size_t>(w);
    }
}

inline void write_all(const char* s) { write_all(s, __builtin_strlen(s)); }

inline void write_hex(std::uintptr_t v)
{
    char buf[2 + 2 * sizeof(v)];
    buf[0] = '0';
    buf[1] = 'x';
    char* p = buf + 2;
    for (int i = static_cast<int>(2 * sizeof(v)) - 1; i >= 0; --i) {
        const unsigned nibble = (v >> (i * 4)) & 0xFu;
        *p++ = static_cast<char>(nibble < 10 ? '0' + nibble : 'a' + (nibble - 10));
    }
    write_all(buf, static_cast<std::size_t>(p - buf));
}
