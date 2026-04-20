/**
 * @file ex07_optimize_layout.cpp
 * @brief 练习：优化结构体布局
 *
 * 将一个布局不佳的 Monster 结构体通过重排成员来减小 sizeof。
 * 展示优化前后的对比。
 *
 * 原始结构（来自教程练习）：
 *   bool is_alive, double health, char name[16], int level,
 *   float speed, uint64_t experience
 */

#include <cstddef>
#include <cstdint>
#include <iostream>

// ============================================================
// 优化前：原始的 Monster 结构体（成员随意排列）
// ============================================================

// 布局分析（64 位系统）：
//   is_alive (bool, 1)     : 偏移 0，大小 1
//   填充                    : 偏移 1-7 (7 字节，对齐 double)
//   health (double, 8)     : 偏移 8，大小 8
//   name (char[16], 16)    : 偏移 16，大小 16
//   level (int, 4)         : 偏移 32，大小 4
//   speed (float, 4)       : 偏移 36，大小 4
//   experience (uint64_t, 8): 偏移 40，大小 8
//   无尾部填充 (48 是 8 的倍数)
//   预测 sizeof = 48

struct MonsterBad {
    bool     is_alive;      // 1 字节
    double   health;        // 8 字节
    char     name[16];      // 16 字节
    int      level;         // 4 字节
    float    speed;         // 4 字节
    uint64_t experience;    // 8 字节
};

// ============================================================
// 优化后：按对齐要求从大到小排列
// ============================================================

// 布局分析（64 位系统）：
//   experience (uint64_t, 8) : 偏移 0，大小 8
//   health (double, 8)       : 偏移 8，大小 8
//   name (char[16], 16)      : 偏移 16，大小 16
//   level (int, 4)           : 偏移 32，大小 4
//   speed (float, 4)         : 偏移 36，大小 4
//   is_alive (bool, 1)       : 偏移 40，大小 1
//   尾部填充                  : 偏移 41-47 (7 字节，整体须为 8 的倍数)
//   预测 sizeof = 48 ... 实际也是 48（和原始一样？）
//
// 等等——这个结构体不管怎么排，因为 char name[16] 和两个 8 字节成员，
// 总大小都至少是 8+8+16+4+4+1=41，向上取整到 48。
// 但填充的位置不同：bad 版有 7 字节内部填充，good 版只有 7 字节尾部填充。
//
// 我们试试更紧凑的排列，把小类型塞进尾部填充：

// 布局分析 v2：
//   experience (uint64_t, 8) : 偏移 0，大小 8
//   health (double, 8)       : 偏移 8，大小 8
//   level (int, 4)           : 偏移 16，大小 4
//   speed (float, 4)         : 偏移 20，大小 4
//   name (char[16], 16)      : 偏移 24，大小 16
//   is_alive (bool, 1)       : 偏移 40，大小 1
//   尾部填充                  : 偏移 41-47 (7 字节)
//   sizeof = 48

struct MonsterGood {
    uint64_t experience;    // 8 字节 (最大对齐)
    double   health;        // 8 字节
    int      level;         // 4 字节
    float    speed;         // 4 字节
    char     name[16];      // 16 字节
    bool     is_alive;      // 1 字节
};

// ============================================================
// 再看一个优化效果更明显的例子
// ============================================================

// BadVersion：随机排列，大量填充
struct ParticleBad {
    char     active;        // 1 字节 -> 偏移 0
    // 填充 7 字节
    double   x;             // 8 字节 -> 偏移 8
    char     layer;         // 1 字节 -> 偏移 16
    // 填充 3 字节
    float    y;             // 4 字节 -> 偏移 20
    char     tag;           // 1 字节 -> 偏移 24
    // 填充 3 字节
    int      id;            // 4 字节 -> 偏移 28
    // sizeof = 32
};

// GoodVersion：按对齐从大到小排列
struct ParticleGood {
    double   x;             // 8 字节 -> 偏移 0
    float    y;             // 4 字节 -> 偏移 8
    int      id;            // 4 字节 -> 偏移 12
    char     active;        // 1 字节 -> 偏移 16
    char     layer;         // 1 字节 -> 偏移 17
    char     tag;           // 1 字节 -> 偏移 18
    // 尾部填充 5 字节 (偏移 19-23, 24 是 8 的倍数)
    // sizeof = 24
};

// ============================================================
// 打印辅助函数
// ============================================================

template <typename T>
void print_layout(const char* name)
{
    std::cout << name << ":\n";
    std::cout << "  sizeof = " << sizeof(T)
              << ", alignof = " << alignof(T) << "\n";
}

int main()
{
    std::cout << "===== ex07: 优化结构体布局 =====\n\n";

    // ---- Monster 结构体对比 ----
    std::cout << "=== Monster 结构体 ===\n";

    print_layout<MonsterBad>("MonsterBad (优化前)");
    std::cout << "  偏移量: is_alive=" << offsetof(MonsterBad, is_alive)
              << ", health=" << offsetof(MonsterBad, health)
              << ", name=" << offsetof(MonsterBad, name)
              << ", level=" << offsetof(MonsterBad, level)
              << ", speed=" << offsetof(MonsterBad, speed)
              << ", experience=" << offsetof(MonsterBad, experience) << "\n\n";

    print_layout<MonsterGood>("MonsterGood (优化后)");
    std::cout << "  偏移量: experience=" << offsetof(MonsterGood, experience)
              << ", health=" << offsetof(MonsterGood, health)
              << ", level=" << offsetof(MonsterGood, level)
              << ", speed=" << offsetof(MonsterGood, speed)
              << ", name=" << offsetof(MonsterGood, name)
              << ", is_alive=" << offsetof(MonsterGood, is_alive) << "\n\n";

    std::cout << "  Monster 大小变化: " << sizeof(MonsterBad) << " -> "
              << sizeof(MonsterGood) << " 字节";
    if (sizeof(MonsterBad) == sizeof(MonsterGood)) {
        std::cout << " (大小相同，但内部填充位置更优)\n";
        std::cout << "  MonsterBad 内部填充: "
                  << sizeof(MonsterBad) - (1+8+16+4+4+8) << " 字节\n";
        std::cout << "  MonsterGood 内部填充: "
                  << sizeof(MonsterGood) - (1+8+16+4+4+8) << " 字节\n";
    }

    // ---- Particle 结构体对比 ----
    std::cout << "\n=== Particle 结构体 (效果更明显) ===\n";

    print_layout<ParticleBad>("ParticleBad (优化前)");
    std::cout << "  偏移量: active=" << offsetof(ParticleBad, active)
              << ", x=" << offsetof(ParticleBad, x)
              << ", layer=" << offsetof(ParticleBad, layer)
              << ", y=" << offsetof(ParticleBad, y)
              << ", tag=" << offsetof(ParticleBad, tag)
              << ", id=" << offsetof(ParticleBad, id) << "\n\n";

    print_layout<ParticleGood>("ParticleGood (优化后)");
    std::cout << "  偏移量: x=" << offsetof(ParticleGood, x)
              << ", y=" << offsetof(ParticleGood, y)
              << ", id=" << offsetof(ParticleGood, id)
              << ", active=" << offsetof(ParticleGood, active)
              << ", layer=" << offsetof(ParticleGood, layer)
              << ", tag=" << offsetof(ParticleGood, tag) << "\n\n";

    std::cout << "  Particle 大小变化: " << sizeof(ParticleBad) << " -> "
              << sizeof(ParticleGood) << " 字节"
              << " (节省 " << sizeof(ParticleBad) - sizeof(ParticleGood)
              << " 字节, "
              << (100 * (sizeof(ParticleBad) - sizeof(ParticleGood)) /
                  sizeof(ParticleBad))
              << "%)\n";

    // ---- 数组中的累积效果 ----
    std::cout << "\n=== 数组中的累积效果 ===\n";
    constexpr int kCount = 1000000;
    std::cout << "  " << kCount << " 个 ParticleBad:  "
              << static_cast<double>(kCount * sizeof(ParticleBad)) / (1024 * 1024)
              << " MB\n";
    std::cout << "  " << kCount << " 个 ParticleGood: "
              << static_cast<double>(kCount * sizeof(ParticleGood)) / (1024 * 1024)
              << " MB\n";
    std::cout << "  节省: "
              << static_cast<double>(kCount * (sizeof(ParticleBad) - sizeof(ParticleGood))) / (1024 * 1024)
              << " MB\n";

    std::cout << "\n要点:\n";
    std::cout << "  1. 经验法则：按对齐要求从大到小排列成员\n";
    std::cout << "  2. double/uint64_t(8) -> int/float(4) -> short(2) -> char/bool(1)\n";
    std::cout << "  3. 相邻的小类型可以共享填充空间\n";
    std::cout << "  4. 对于结构体数组，节省的字节会线性累积\n";
    std::cout << "  5. 不改变任何逻辑，仅重排声明顺序即可优化\n";

    return 0;
}
