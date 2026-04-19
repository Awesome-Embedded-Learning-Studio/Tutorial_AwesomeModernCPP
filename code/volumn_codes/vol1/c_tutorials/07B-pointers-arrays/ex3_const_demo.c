#include <stdio.h>

int main(void)
{
    int x = 10;
    int y = 20;

    // Pointer to const int: cannot modify *p1, but can change where p1 points
    const int* p1 = &x;
    // *p1 = 50;    // ERROR: cannot modify const data
    p1 = &y;        // OK: can change pointer itself
    printf("const int* p1 -> *p1 = %d\n", *p1);

    // Const pointer to int: can modify *p2, but cannot change where p2 points
    int* const p2 = &x;
    *p2 = 50;       // OK: data is not const
    // p2 = &y;     // ERROR: cannot change const pointer
    printf("int* const p2 -> *p2 = %d\n", *p2);

    // Const pointer to const int: cannot modify anything
    const int* const p3 = &x;
    // *p3 = 100;   // ERROR
    // p3 = &y;     // ERROR
    printf("const int* const p3 -> *p3 = %d\n", *p3);

    return 0;
}
