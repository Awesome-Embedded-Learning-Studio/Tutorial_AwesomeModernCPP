bool safe_less(int a, unsigned b) {
    if (a < 0) {
        return true;
    }
    return static_cast<unsigned>(a) < b;
}
