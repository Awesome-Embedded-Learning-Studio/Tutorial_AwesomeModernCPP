#include <stdio.h>
#include <stddef.h>

const int* linear_search(const int* data, size_t count, int target)
{
    for (const int* p = data; p < data + count; p++) {
        if (*p == target) {
            return p;
        }
    }
    return NULL;
}

int main(void)
{
    int data[] = {5, 12, 7, 3, 19, 8};
    size_t count = sizeof(data) / sizeof(data[0]);

    int targets[] = {19, 5, 42};
    for (int i = 0; i < 3; i++) {
        const int* result = linear_search(data, count, targets[i]);
        if (result != NULL) {
            printf("Found %d at index %zd (address offset: %td)\n",
                   targets[i], result - data, result - data);
        } else {
            printf("%d not found\n", targets[i]);
        }
    }

    return 0;
}
