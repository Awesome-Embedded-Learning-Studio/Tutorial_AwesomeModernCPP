#include <cstdio>
#ifdef LEAK_SANITIZER
const char* m = "DEFINED";
#else
const char* m = "NOT defined";
#endif
int main() {
    std::puts(m);
    return 0;
}
