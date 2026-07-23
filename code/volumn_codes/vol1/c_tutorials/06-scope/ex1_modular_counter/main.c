#include "counter.h"
#include <stdio.h>

int main(void) {
    printf("%d\n", counter_get()); // 输出应当是0
    counter_increment();
    printf("%d\n", counter_get()); // 输出应当是1
    counter_increment();
    printf("%d\n", counter_get()); // 输出应当是2
    counter_reset();
    printf("%d\n", counter_get()); // 输出应当是0
    return 0;
}
