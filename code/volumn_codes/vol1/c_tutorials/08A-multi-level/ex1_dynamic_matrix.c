#include <stdio.h>
#include <stdlib.h>

int** allocate_matrix(int rows, int cols)
{
    int** matrix = malloc((size_t)rows * sizeof(int*));
    if (!matrix) return NULL;

    for (int i = 0; i < rows; i++) {
        matrix[i] = malloc((size_t)cols * sizeof(int));
        if (!matrix[i]) {
            // Cleanup on failure
            for (int j = 0; j < i; j++) free(matrix[j]);
            free(matrix);
            return NULL;
        }
    }
    return matrix;
}

void free_matrix(int** matrix, int rows)
{
    if (!matrix) return;
    for (int i = 0; i < rows; i++) {
        free(matrix[i]);
    }
    free(matrix);
}

void fill_matrix(int** matrix, int rows, int cols, int value)
{
    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            matrix[i][j] = value + i * cols + j;
        }
    }
}

void print_matrix(int** matrix, int rows, int cols)
{
    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            printf("%4d", matrix[i][j]);
        }
        printf("\n");
    }
}

int main(void)
{
    int rows = 3, cols = 5;

    int** mat = allocate_matrix(rows, cols);
    if (!mat) {
        fprintf(stderr, "Allocation failed\n");
        return 1;
    }

    fill_matrix(mat, rows, cols, 1);
    print_matrix(mat, rows, cols);

    free_matrix(mat, rows);
    return 0;
}
