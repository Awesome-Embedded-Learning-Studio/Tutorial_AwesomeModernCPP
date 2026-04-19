#include "stack.h"

#include <stdio.h>

int main(void)
{
    Stack* s = stack_create(5);
    if (!s) {
        fprintf(stderr, "Failed to create stack\n");
        return 1;
    }

    for (int i = 10; i <= 50; i += 10) {
        stack_push(s, i);
        printf("Pushed %d, size=%d\n", i, stack_size(s));
    }

    // This should fail (full)
    printf("Push 60: %s\n", stack_push(s, 60) == 0 ? "OK" : "FAILED (full)");

    int val;
    while (stack_pop(s, &val) == 0) {
        printf("Popped %d, remaining=%d\n", val, stack_size(s));
    }

    stack_destroy(s);
    return 0;
}
