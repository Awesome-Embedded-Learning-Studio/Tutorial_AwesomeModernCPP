// Test: Does constexpr function really degrade to runtime when assigned to non-constexpr variable?
#include <cstdio>

// Simple constexpr function
constexpr int square(int x) {
    return x * x;
}

// Test function to examine assembly
void test_constexpr_assignment() {
    int runtime_val = 42;
    int result = square(runtime_val);

    printf("square(42) = %d\n", result);
}

// Test with constexpr variable
void test_constexpr_variable() {
    constexpr int kResult = square(42);

    printf("constexpr square(42) = %d\n", kResult);
}

int main() {
    test_constexpr_assignment();
    test_constexpr_variable();
    return 0;
}
