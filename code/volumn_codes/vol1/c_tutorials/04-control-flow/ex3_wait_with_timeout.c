#include <stdio.h>
#include <time.h>

int wait_with_timeout(int (*check)(void), unsigned int timeout_ms)
{
    clock_t start = clock();
    clock_t deadline = start + (clock_t)(timeout_ms * CLOCKS_PER_SEC / 1000);

    while (clock() < deadline) {
        if (check()) {
            return 0;
        }
    }
    return -1;
}

// Simulated check: succeeds after being called 3 times
static int check_counter = 0;
int simulated_check(void)
{
    check_counter++;
    printf("  check() called (#%d)\n", check_counter);
    return check_counter >= 3;
}

int main(void)
{
    printf("Testing wait_with_timeout (should succeed):\n");
    int result = wait_with_timeout(simulated_check, 5000);
    printf("Result: %s\n\n", result == 0 ? "condition met" : "timeout");

    check_counter = 100;  // Never reaches 103 quickly
    printf("Testing wait_with_timeout (should timeout):\n");
    result = wait_with_timeout(simulated_check, 10);
    printf("Result: %s\n", result == 0 ? "condition met" : "timeout");

    return 0;
}
