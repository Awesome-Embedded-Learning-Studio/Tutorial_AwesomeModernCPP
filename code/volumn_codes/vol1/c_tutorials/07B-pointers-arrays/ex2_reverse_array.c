#include <stdio.h>

void reverse_array(int* data, size_t count);

int main(void) {
    int arr[] = {10, 21, 32, 43, 54, 65, 76, 87, 98, 9};
    int arr_size = sizeof(arr) / sizeof(arr[0]);

    printf("before reverse_array:\n");
    for (int i = 0; i < arr_size; i++) {
        printf("%d\n", arr[i]);
    }

    reverse_array(arr, arr_size);

    printf("after reverse_array:\n");
    for (int i = 0; i < arr_size; i++) {
        printf("%d\n", arr[i]);
    }

    return 0;
}

void reverse_array(int* data, size_t count) {
    if (data == NULL) {
        printf("data is null\n");
        return;
    }
    if (count == 0) {
        printf("data is empty\n");
        return;
    }

    int* start = data;
    int* end = data + count - 1;
    int temp = 0;
    while (start < end) {
        temp = *start;
        *start = *end;
        *end = temp;
        start++;
        end--;
    }
}
