#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stddef.h>

size_t safe_str_copy(char* dst, const char* src, size_t dst_size)
{
    if (dst_size == 0) return 0;
    size_t src_len = strlen(src);
    size_t copy_len = src_len < dst_size - 1 ? src_len : dst_size - 1;
    memcpy(dst, src, copy_len);
    dst[copy_len] = '\0';
    return copy_len;
}

size_t safe_str_cat(char* dst, const char* src, size_t dst_size)
{
    if (dst_size == 0) return 0;
    size_t dst_len = strlen(dst);
    if (dst_len >= dst_size) return 0;

    size_t remaining = dst_size - dst_len - 1;
    size_t src_len = strlen(src);
    size_t copy_len = src_len < remaining ? src_len : remaining;
    memcpy(dst + dst_len, src, copy_len);
    dst[dst_len + copy_len] = '\0';
    return copy_len;
}

size_t safe_str_format(char* dst, size_t dst_size, const char* format, ...)
{
    if (dst_size == 0) return 0;
    va_list args;
    va_start(args, format);
    int result = vsnprintf(dst, dst_size, format, args);
    va_end(args);

    if (result < 0) {
        dst[0] = '\0';
        return 0;
    }
    size_t written = (size_t)result < dst_size ? (size_t)result : dst_size - 1;
    return written;
}

int main(void)
{
    char buf[16];

    safe_str_copy(buf, "Hello", sizeof(buf));
    printf("copy: \"%s\" (len=%zu)\n", buf, strlen(buf));

    safe_str_cat(buf, ", World!", sizeof(buf));
    printf("cat:  \"%s\" (len=%zu)\n", buf, strlen(buf));

    // Truncation test
    safe_str_copy(buf, "This string is way too long for the buffer", sizeof(buf));
    printf("truncated: \"%s\" (len=%zu)\n", buf, strlen(buf));

    safe_str_format(buf, sizeof(buf), "value=%d, pi=%.2f", 42, 3.14159);
    printf("format: \"%s\"\n", buf);

    return 0;
}
