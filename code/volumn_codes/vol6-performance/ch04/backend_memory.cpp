// backend_memory.cpp — vol6 ch04-01 后端内存
// A. AoS vs SoA:粒子「只更新位置」场景,SoA 比 AoS 快多少
// B. 结构体对齐/padding:sizeof 与 cacheline 利用率
// C. 软件 prefetch 在不规则访问里有没有用
// 编译: g++ -O2 -std=c++17 backend_memory.cpp -o backend_memory
// 跑:   taskset -c 0 ./backend_memory
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

using clk = std::chrono::steady_clock;
inline void do_not_optimize(float v) {
    uint32_t u;
    __builtin_memcpy(&u, &v, 4);
    asm volatile("" : "+r"(u)::"memory");
}
inline void do_not_optimize_p(const void* p) {
    asm volatile("" : "+r"(p)::"memory");
}

constexpr int N = 1 << 20; // 100 万粒子

// ----- A. AoS vs SoA -----
struct ParticleAoS {
    float x, y, z, vx, vy, vz;
}; // 24B,无 pad
struct ParticleAoS8 {
    float x, y, z, vx, vy, vz, pad1, pad2;
}; // 32B(填到 8 的倍数)

void update_aos_x(ParticleAoS* p, int n) {
    for (int i = 0; i < n; ++i)
        p[i].x += p[i].vx * 0.016f; // 只动 x,但每行把 vx..vz 也拉进 cache
}
struct ParticlesSoA {
    std::vector<float> x, y, z, vx, vy, vz;
    ParticlesSoA(int n) : x(n), y(n), z(n), vx(n), vy(n), vz(n) {}
};
void update_soa_x(ParticlesSoA& p, int n) {
    for (int i = 0; i < n; ++i)
        p.x[i] += p.vx[i] * 0.016f; // 只碰 x 和 vx 数组
}

template <class F> double time_ms(F&& f, int repeat) {
    f();
    f(); // warmup
    auto t0 = clk::now();
    for (int r = 0; r < repeat; ++r)
        f();
    auto t1 = clk::now();
    return std::chrono::duration<double, std::milli>(t1 - t0).count() / repeat;
}

int main() {
    // ===== B. sizeof 与 cacheline 利用率 =====
    std::printf("===== B. 结构体布局 =====\n");
    std::printf("  sizeof(ParticleAoS)  = %zu B\n", sizeof(ParticleAoS));
    std::printf("  sizeof(ParticleAoS8) = %zu B(pad 到 32)\n", sizeof(ParticleAoS8));
    std::printf("  64B cacheline 能装:AoS=%zu 个,AoS8=%zu 个\n", 64 / sizeof(ParticleAoS),
                64 / sizeof(ParticleAoS8));

    // ===== A. AoS vs SoA =====
    std::vector<ParticleAoS> aos(N);
    ParticlesSoA soa(N);
    for (int i = 0; i < N; ++i) {
        aos[i] = {0, 0, 0, (float)i, (float)i, (float)i};
        soa.x[i] = 0;
        soa.vx[i] = (float)i;
    }
    double t_aos = time_ms([&] { update_aos_x(aos.data(), N); }, 20);
    double t_soa = time_ms([&] { update_soa_x(soa, N); }, 20);
    std::printf("\n===== A. AoS vs SoA(更新 %d 粒子的 x,20 次平均)=====\n", N);
    std::printf("  AoS:%6.2f ms/次(每行把不更新的 y/z/vy/vz 也拉进 cache,带宽浪费)\n", t_aos);
    std::printf("  SoA:%6.2f ms/次(只碰 x、vx 数组,cacheline 利用率近 100%%)\n", t_soa);
    std::printf("  SoA 快 %.2fx\n", t_aos / t_soa);
    return 0;
}
