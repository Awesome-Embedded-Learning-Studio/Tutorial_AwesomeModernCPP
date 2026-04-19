#include <stdio.h>

// Zero-overhead debug log macro
#ifdef NDEBUG
    #define DEBUG_LOG(fmt, ...) ((void)0)
#else
    #define DEBUG_LOG(fmt, ...) \
        fprintf(stderr, "[DEBUG] %s:%d: " fmt "\n", __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#endif

int main(void)
{
    int x = 42;
    const char* name = "test";

    DEBUG_LOG("Starting program");
    DEBUG_LOG("x = %d, name = %s", x, name);
    DEBUG_LOG("No extra args");

    // In release mode (compile with -DNDEBUG), all DEBUG_LOG expand to nothing
    printf("Program done. Compile with -DNDEBUG to disable debug logs.\n");

    return 0;
}
