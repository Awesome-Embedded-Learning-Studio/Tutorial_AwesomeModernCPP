// owollz4 原稿,原样保留(约定的正整数输入下,与辗转相除等价)
int gcd(int n, int m) {
    if (n <= 0 || m <= 0)
        return -1;
    if (n > m) {
        if (n % m == 0)
            return m;
        return gcd(m, n % m);
    }
    if (n < m) {
        if (m % n == 0)
            return n;
        return gcd(n, m % n);
    }
    return n;
}
