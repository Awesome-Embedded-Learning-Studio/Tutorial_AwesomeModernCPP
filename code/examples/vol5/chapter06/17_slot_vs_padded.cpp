// slot_vs_padded.cpp —— alignas(64) 与手工垫字节：尺寸都对，对齐不同
#include <cstdio>

struct alignas(64) Slot   { long long v = 0; };      // 一人一条行
struct Padded             { long long v = 0; char pad[56]; };   // 手工垫满，也能用

int main() {
  std::printf("sizeof(Slot)=%zu   alignof(Slot)=%zu\n", sizeof(Slot), alignof(Slot));
  std::printf("sizeof(Padded)=%zu alignof(Padded)=%zu\n", sizeof(Padded), alignof(Padded));
  return 0;
}
