#include <cstdio>

// owollz4 原稿:words_upper 原样保留,main 是判题程序的入口壳
void words_upper() {
    int ch = 0;
    int dif = 'A' - 'a';
    while ((ch = getchar()) != EOF) {
        if (ch >= 'a' && ch <= 'z')
            putchar((ch + dif));
        else
            putchar(ch);
    }
    return;
}

int main() {
    words_upper();
    return 0;
}
