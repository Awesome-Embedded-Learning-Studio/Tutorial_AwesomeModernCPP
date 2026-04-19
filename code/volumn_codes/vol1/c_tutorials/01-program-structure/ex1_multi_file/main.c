#include "utils.h"

#include <stdio.h>

int main(void)
{
    int a = 10, b = 20;
    print_result("10 + 20", add(a, b));
    print_result("100 + (-50)", add(100, -50));
    print_result("0 + 0", add(0, 0));
    print_result("INT_MAX + 1", add(2147483647, 1));
    return 0;
}
