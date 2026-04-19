#include <stdio.h>

int main(void)
{
    int    i = 42;
    double d = 3.14;
    char   c = 'A';

    printf("int    i=%d,  &i=%p, sizeof(i)=%zu\n", i, (void*)&i, sizeof(i));
    printf("double d=%.2f, &d=%p, sizeof(d)=%zu\n", d, (void*)&d, sizeof(d));
    printf("char   c='%c', &c=%p, sizeof(c)=%zu\n", c, (void*)&c, sizeof(c));

    printf("\nObserve: address spacing often matches sizeof(type)\n");

    return 0;
}
