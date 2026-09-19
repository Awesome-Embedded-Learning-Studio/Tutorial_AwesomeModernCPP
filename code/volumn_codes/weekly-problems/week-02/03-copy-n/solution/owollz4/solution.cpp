void copy_n(char dst[], char src[], int n) {
    for (int i = 0; i < n; i++) {
        dst[i] = src[i];
        if (src[i] == '\0') {
            for (i++; i < n; i++) {
                dst[i] = '\0';
            }
        }
    }
}
