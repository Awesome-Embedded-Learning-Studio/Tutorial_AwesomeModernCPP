#include <stdio.h>

// sizeof returns size_t (unsigned). Comparing with a signed int causes
// implicit conversion: the int is promoted to unsigned, so -1 becomes
// a very large positive number and the comparison gives unexpected results.
int main(void)
{
    int arr[] = {1, 2, 3, 4, 5};
    int i = -1;

    // Bug: i is converted to unsigned, becomes SIZE_MAX-like value
    if (i < (int)sizeof(arr) / (int)sizeof(arr[0])) {
        printf("Element access safe (with cast)\n");
    } else {
        printf("Out of bounds! (unexpected)\n");
    }

    // Fix: explicit cast makes intent clear
    if (i >= 0 && (size_t)i < sizeof(arr) / sizeof(arr[0])) {
        printf("arr[%d] = %d\n", i, arr[i]);
    } else {
        printf("Index %d is out of bounds\n", i);
    }

    return 0;
}
