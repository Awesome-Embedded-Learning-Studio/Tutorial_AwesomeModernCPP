#include <cstdlib>

int digit_distance(int n) {
    if (n < 10) return 0;  // 只剩一位:没有相邻对
    return std::abs(n % 10 - (n / 10) % 10) + digit_distance(n / 10);
}
