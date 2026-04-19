#include <stdio.h>

void matrix_transpose(int rows, int cols, const int src[rows][cols], int dst[cols][rows])
{
    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            dst[j][i] = src[i][j];
        }
    }
}

void matrix_multiply(int m, int n, int p,
                     const int a[m][n], const int b[n][p], int c[m][p])
{
    for (int i = 0; i < m; i++) {
        for (int j = 0; j < p; j++) {
            c[i][j] = 0;
            for (int k = 0; k < n; k++) {
                c[i][j] += a[i][k] * b[k][j];
            }
        }
    }
}

void matrix_print(int rows, int cols, const int mat[rows][cols])
{
    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            printf("%4d", mat[i][j]);
        }
        printf("\n");
    }
}

int main(void)
{
    int a[2][3] = {{1, 2, 3}, {4, 5, 6}};
    int at[3][2];

    printf("Original (2x3):\n");
    matrix_print(2, 3, a);

    matrix_transpose(2, 3, a, at);
    printf("\nTransposed (3x2):\n");
    matrix_print(3, 2, at);

    int b[3][2] = {{1, 4}, {2, 5}, {3, 6}};
    int c[2][2];
    matrix_multiply(2, 3, 2, a, b, c);
    printf("\nA x B (2x2):\n");
    matrix_print(2, 2, c);

    return 0;
}
