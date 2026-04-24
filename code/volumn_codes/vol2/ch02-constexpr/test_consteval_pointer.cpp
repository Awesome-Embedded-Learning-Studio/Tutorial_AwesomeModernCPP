// Test: Can consteval function pointers be used at runtime?
#include <cstdio>

consteval int square(int x) {
    return x * x;
}

// Test 1: Can we take address of consteval function?
void test_address() {
    // This should fail in consteval context
    // auto ptr = &square;
    // int result = (*ptr)(5);
}

// Test 2: Using consteval function through function pointer in constexpr context
constexpr int call_via_constexpr_ptr() {
    // constexpr auto ptr = &square;  // Should this work?
    // return ptr(5);
    return square(5);  // This definitely works
}

int main() {
    constexpr int result = call_via_constexpr_ptr();
    printf("square(5) = %d\n", result);
    return 0;
}
