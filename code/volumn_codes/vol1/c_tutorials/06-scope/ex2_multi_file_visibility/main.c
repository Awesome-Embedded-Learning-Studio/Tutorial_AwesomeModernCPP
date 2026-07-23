#include "a.h"
#include "b.h"
#include <stdio.h>

int main(void) {

    a_greet();                    // 调用 a 模块的公共函数，触发它内部的 helper_a
    printf("%d\n", kSharedValue); // 输出应当是0
    set_kSharedValue(100);        // 内部会调用 b 模块自己的 helper_a
    printf("%d\n", kSharedValue); // 输出应当是100

    return 0;
}
