#include "a.h"
#include <stdio.h>

int kSharedValue = 0;
static void helper_a(void) {
    printf("need help?\n");
}

// a.c 暴露的公共函数，内部调用文件私有的 helper_a
void a_greet(void) {
    helper_a();
}
