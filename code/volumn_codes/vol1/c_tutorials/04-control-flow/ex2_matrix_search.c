#include <stdio.h>
#include <stdlib.h>

typedef struct {
    int row;
    int col;
    int found;
} SearchResult;

SearchResult matrix_search_flag(int** matrix, int rows, int cols, int target)
{
    SearchResult result = {-1, -1, 0};
    int done = 0;
    for (int i = 0; i < rows && !done; i++) {
        for (int j = 0; j < cols && !done; j++) {
            if (matrix[i][j] == target) {
                result.row = i;
                result.col = j;
                result.found = 1;
                done = 1;
            }
        }
    }
    return result;
}

SearchResult matrix_search_goto(int** matrix, int rows, int cols, int target)
{
    SearchResult result = {-1, -1, 0};
    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            if (matrix[i][j] == target) {
                result.row = i;
                result.col = j;
                result.found = 1;
                goto found;
            }
        }
    }
found:
    return result;
}

int main(void)
{
    int* rows[3];
    int r0[] = {1, 2, 3};
    int r1[] = {4, 5, 6};
    int r2[] = {7, 8, 9};
    rows[0] = r0; rows[1] = r1; rows[2] = r2;

    int** mat = rows;
    int target = 5;

    SearchResult r1_result = matrix_search_flag(mat, 3, 3, target);
    printf("Flag version: found=%d at (%d, %d)\n",
           r1_result.found, r1_result.row, r1_result.col);

    SearchResult r2_result = matrix_search_goto(mat, 3, 3, target);
    printf("Goto version: found=%d at (%d, %d)\n",
           r2_result.found, r2_result.row, r2_result.col);

    return 0;
}
