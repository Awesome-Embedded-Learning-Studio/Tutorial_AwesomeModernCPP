#include "utils.h"

#include <stdio.h>

int add(int a, int b)
{
    return a + b;
}

void print_result(const char* label, int value)
{
    printf("%s: %d\n", label, value);
}
