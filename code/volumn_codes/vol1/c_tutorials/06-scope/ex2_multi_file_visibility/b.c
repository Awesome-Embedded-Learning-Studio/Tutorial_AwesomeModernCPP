#include "b.h"
#include "a.h"
#include <stdio.h>

static void helper_a(void) {
    printf("need help?\n");
}
void set_kSharedValue(int value) {
    helper_a();
    kSharedValue = value;
}
