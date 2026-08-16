#include <cstdio>

int main() {
#ifdef NDEBUG
    std::puts("release build (NDEBUG defined)");
#else
    std::puts("debug build (NDEBUG NOT defined)");
#endif
    return 0;
}
