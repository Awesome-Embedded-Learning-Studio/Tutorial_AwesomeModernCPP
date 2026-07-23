#include <stddef.h> // size_t
#include <stdio.h>

void print_int_array(const int* data, size_t count);

int main(void) {
    int arr[] = {1, 2, 3, 4, 5};
    print_int_array(arr, 5);
    return 0;
}

void print_int_array(const int* data, size_t count) {
    for (size_t i = 0; i < count; i++) {
        printf("data[%zu] = %d\n", i, *(data + i));
    }
}
