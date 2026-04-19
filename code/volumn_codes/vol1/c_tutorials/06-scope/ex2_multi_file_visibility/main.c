#include "a.h"
#include "b.h"

#include <stdio.h>

int main(void)
{
    printf("kSharedValue (extern) = %d\n\n", kSharedValue);

    print_from_a();
    printf("\n");
    print_from_b();
    printf("\n");

    set_shared_value(99);

    return 0;
}
