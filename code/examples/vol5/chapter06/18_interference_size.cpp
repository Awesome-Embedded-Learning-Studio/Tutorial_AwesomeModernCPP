#include <cstddef>
#include <cstdio>
#include <new>

int main() {
#ifdef __cpp_lib_hardware_interference_size
  std::printf("%zu\n", std::hardware_destructive_interference_size);
#else
  std::printf("constant unavailable\n");
#endif
}
