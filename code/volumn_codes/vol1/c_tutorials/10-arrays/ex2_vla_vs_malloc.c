#include <stdio.h>
#include <stdlib.h>

void fill_with_vla(int n, int arr[n])
{
    for (int i = 0; i < n; i++) {
        arr[i] = i * i;
    }
}

int* fill_with_malloc(int n)
{
    int* arr = malloc((size_t)n * sizeof(int));
    if (!arr) return NULL;
    for (int i = 0; i < n; i++) {
        arr[i] = i * i;
    }
    return arr;
}

int main(void)
{
    int n = 10;

    // VLA version: stack allocated, no explicit free needed
    int vla_arr[n];
    fill_with_vla(n, vla_arr);
    printf("VLA:  ");
    for (int i = 0; i < n; i++) printf("%d ", vla_arr[i]);
    printf("\n");

    // malloc version: heap allocated, must free
    int* heap_arr = fill_with_malloc(n);
    if (heap_arr) {
        printf("malloc: ");
        for (int i = 0; i < n; i++) printf("%d ", heap_arr[i]);
        printf("\n");
        free(heap_arr);
    } else {
        printf("malloc: allocation failed\n");
    }

    printf("\nKey differences:\n");
    printf("  VLA:    stack, auto-freed, no NULL check, may overflow on huge n\n");
    printf("  malloc: heap, must free, can check NULL, suitable for large data\n");

    return 0;
}
