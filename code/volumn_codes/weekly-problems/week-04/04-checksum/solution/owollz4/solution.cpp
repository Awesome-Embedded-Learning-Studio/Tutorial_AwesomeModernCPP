#include <cstdio>

// owollz4 原稿:累加与打印原样保留,main 是判题程序的入口壳
void checksum() {
    int ch = 0;
    signed char sum = -1;
    while ((ch = getchar()) != EOF) {
        putchar(ch); // 落题时补的一行
        sum += ch;
    }
    printf("\n%d\n", sum);
}

int main() {
    checksum();
    return 0;
}
