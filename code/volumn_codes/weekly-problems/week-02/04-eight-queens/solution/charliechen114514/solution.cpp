int col[10]; // col[r] = 第 r 行的皇后放在第几列(到 n = 9 都够用)
int total;

bool ok(int row) {
    for (int r = 0; r < row; r++) {
        if (col[r] == col[row])
            return false; // 同列
        if (col[r] - col[row] == r - row)
            return false; // 主对角线
        if (col[r] - col[row] == row - r)
            return false; // 副对角线
    }
    return true;
}

void dfs(int row, int n) {
    if (row == n) { // n 行都放满了,凑出一组解
        total++;
        return;
    }
    for (int c = 0; c < n; c++) {
        col[row] = c;
        if (ok(row))
            dfs(row + 1, n);
    }
}

int count_solutions(int n) {
    total = 0;
    dfs(0, n);
    return total;
}
