#include <stdio.h>

int main(void)
{
    printf("[%5d]\n", 42);
    printf("[%-5d]\n", 42);
    printf("[%05d]\n", 42);
    printf("[%.3f]\n", 3.14159);
    printf("[%10.2f]\n", 3.14159);
    return 0;
}
