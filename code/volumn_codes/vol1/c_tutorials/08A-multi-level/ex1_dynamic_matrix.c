#include <stdio.h>
#include <stdlib.h>

int** allocate_matrix(int rows, int cols) {
    if (rows < 1 || cols < 1) {
        return NULL;
    }

    int** matrix = malloc(sizeof(int*) * rows);
    if (matrix == NULL) {
        return NULL; // 防止内存分配失败，但这不太可能吧...以防万一，还是写上吧，如果程序在1978年的硬件上跑呢(笑)
    }

    for (int i = 0; i < rows; i++) {
        matrix[i] = malloc(sizeof(int) * cols);
        if (matrix[i] == NULL) { // 内存不够，取消分配，并释放内存。
            for (int j = 0; j < i; j++) {
                free(matrix[j]);
            }
            free(matrix);
            return NULL;
        }
    }
    return matrix;
}

void free_matrix(int** matrix, int rows) {
    if (matrix == NULL) {
        return;
    }

    for (int i = 0; i < rows; i++) {
        free(matrix[i]);
    }
    free(matrix);
}

void fill_matrix(int** matrix, int rows, int cols, int value) {
    if (rows < 1 || cols < 1 || matrix == NULL) {
        return;
    }

    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            matrix[i][j] = value;
        }
    }
}

int main(void) {
    int rows = 3;
    int cols = 4;

    int** matrix = allocate_matrix(rows, cols);
    if (matrix == NULL) {
        printf("allocate_matrix failed\n");
        return 1;
    }

    fill_matrix(matrix, rows, cols, 7);

    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            printf("%d ", matrix[i][j]);
        }
        printf("\n");
    }

    free_matrix(matrix, rows);
    return 0;
}
