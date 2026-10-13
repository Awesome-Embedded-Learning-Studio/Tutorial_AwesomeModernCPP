#include <cstdarg>

// owollz4 原稿,原样保留
int max_list(int first, ...) {
    if (first < 0)
        return -1;
    va_list args;
    int max = first;
    va_start(args, first);
    int tmp;
    while ((tmp = va_arg(args, int)) >= 0) {
        if (tmp > max)
            max = tmp;
    }
    va_end(args);
    return max;
}
