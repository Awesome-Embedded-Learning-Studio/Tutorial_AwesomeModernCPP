#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <stdlib.h>

void insertion_sort(void* base, size_t nmemb, size_t size,
                    int (*compar)(const void*, const void*))
{
    char* arr = (char*)base;
    char* tmp = malloc(size);

    for (size_t i = 1; i < nmemb; i++) {
        char* key = arr + i * size;
        size_t j = i;
        while (j > 0 && compar(arr + (j - 1) * size, key) > 0) {
            j--;
        }
        if (j < i) {
            memcpy(tmp, key, size);
            memmove(arr + (j + 1) * size, arr + j * size, (i - j) * size);
            memcpy(arr + j * size, tmp, size);
        }
    }

    free(tmp);
}

static int cmp_int_asc(const void* a, const void* b)
{
    return *(const int*)a - *(const int*)b;
}

static int cmp_int_desc(const void* a, const void* b)
{
    return *(const int*)b - *(const int*)a;
}

static int cmp_str(const void* a, const void* b)
{
    return strcmp(*(const char* const*)a, *(const char* const*)b);
}

static void print_ints(const int* arr, size_t n)
{
    for (size_t i = 0; i < n; i++) printf("%d ", arr[i]);
    printf("\n");
}

int main(void)
{
    int nums[] = {5, 2, 8, 1, 9, 3};
    size_t n = sizeof(nums) / sizeof(nums[0]);

    insertion_sort(nums, n, sizeof(int), cmp_int_asc);
    printf("Ascending: "); print_ints(nums, n);

    insertion_sort(nums, n, sizeof(int), cmp_int_desc);
    printf("Descending: "); print_ints(nums, n);

    const char* strs[] = {"banana", "apple", "cherry", "date"};
    size_t sn = sizeof(strs) / sizeof(strs[0]);
    insertion_sort(strs, sn, sizeof(const char*), cmp_str);
    printf("Strings: ");
    for (size_t i = 0; i < sn; i++) printf("%s ", strs[i]);
    printf("\n");

    return 0;
}
