// 03-boundaries —— 段边界的三组动态观察
//
// E3-1  malloc 递增尺寸(1KB → 64MB,在 128KiB 附近细扫):每笔分配后回读
//       /proc/self/maps,判定指针落在 [heap](brk)还是匿名映射(mmap),
//       找出 malloc 从 brk 切到 mmap 的分水岭。所有块全程持有不 free,
//       避免干扰 glibc 的动态阈值。
// E3-2  栈:alloca 逐步下探 + 深递归,观察地址递减方向与 [stack] 段的扩张。
// E3-3  归还:free 64MB 大块后 mmap 映射是否消失;256×48KB 小块喂大 [heap]
//       再全 free,观察 brk 是否收缩(glibc 的 trim);释放 4MB mmap 块后
//       glibc 动态 mmap 阈值会被抬高,随后 3MB 分配回到 [heap]。
#include <sys/mman.h>
#include <sys/resource.h>

#include <alloca.h>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <malloc.h>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

struct Range {
    unsigned long start = 0, end = 0;
    bool found = false;
};

// 一行行读 /proc/self/maps,找目标信息(避免整表解析,这里只要两类答案)
Range heap_range() { // [heap] 段
    Range r;
    FILE* f = fopen("/proc/self/maps", "r");
    char line[512];
    while (f && fgets(line, sizeof line, f)) {
        unsigned long s, e;
        char path[128];
        if (sscanf(line, "%lx-%lx %*s %*s %*s %*s %127s", &s, &e, path) == 3) {
            if (std::string(path) == "[heap]") {
                r = {s, e, true};
                break;
            }
        }
    }
    if (f)
        fclose(f);
    return r;
}

Range mapping_of(unsigned long addr, bool* anon) { // 包含 addr 的映射
    Range r;
    if (anon)
        *anon = false;
    FILE* f = fopen("/proc/self/maps", "r");
    char line[512];
    while (f && fgets(line, sizeof line, f)) {
        unsigned long s, e;
        char path[160];
        int n = sscanf(line, "%lx-%lx %*s %*s %*s %*s %159[^\n]", &s, &e, path);
        if (addr >= s && addr < e) {
            r = {s, e, true};
            if (anon) {
                // 行尾没有路径字段 → 匿名
                *anon = (n == 2);
            }
            break;
        }
    }
    if (f)
        fclose(f);
    return r;
}

// malloc 判定:落在 [heap] → brk;落在匿名映射 → mmap
const char* classify_ptr(void* p, Range* hit) {
    Range m = mapping_of((unsigned long)p, nullptr);
    *hit = m;
    if (!m.found)
        return "!!未命中!!";
    Range h = heap_range();
    if (h.found && (unsigned long)p >= h.start && (unsigned long)p < h.end)
        return "brk([heap])";
    return "mmap(匿名)";
}

} // namespace

int main() {
    // ---------- E3-1 malloc 尺寸扫描 ----------
    constexpr size_t sizes[] = {1024,   4096,   16384,  65536,  98304,   114688,  122880,  126976,
                                130048, 131008, 131048, 131049, 131050,  131056,  131072,  131073,
                                135168, 163840, 196608, 262144, 1048576, 4194304, 67108864};
    Range h0 = heap_range();
    std::printf("== E3-1 malloc 尺寸扫描(起始 [heap]=%lx-%lx) ==\n", h0.start, h0.end);
    std::printf("%10s  %-14s %-12s %-14s %s\n", "请求", "指针", "来源", "[heap]end", "所在映射");
    std::vector<void*> keep(sizeof(sizes) / sizeof(sizes[0]), nullptr);
    long first_mmap_req = -1;
    unsigned long prev_end = h0.end;
    for (size_t i = 0; i < keep.size(); ++i) {
        keep[i] = std::malloc(sizes[i]);
        ((char*)keep[i])[0] = 1; // 碰首字节,保证不是死分配
        Range hit;
        const char* src = classify_ptr(keep[i], &hit);
        Range h = heap_range();
        if (first_mmap_req < 0 && std::string(src).rfind("mmap", 0) == 0)
            first_mmap_req = (long)sizes[i];
        std::printf("%10zu  %-14p %-12s %lx(+%lu) %lx-%lx(%luKB)\n", sizes[i], keep[i], src, h.end,
                    h.end - prev_end, hit.start, hit.end, (hit.end - hit.start) / 1024);
        prev_end = h.end;
    }
    std::printf("分水岭:第一笔走 mmap 的请求 = %ld 字节(glibc 默认 mmap 阈值 128KiB=131072)\n",
                first_mmap_req);

    // ---------- E3-2 栈:alloca 下探 + 深递归 ----------
    struct rlimit rl;
    getrlimit(RLIMIT_STACK, &rl);
    Range stack_before = mapping_of((unsigned long)&rl, nullptr);
    std::printf("\n== E3-2 栈增长([stack] 起初 %lx-%lx,RLIMIT_STACK=%luKB) ==\n",
                stack_before.start, stack_before.end, rl.rlim_cur / 1024);

    std::printf("[alloca] 每轮 4KB,共 200 轮:\n");
    for (int i = 0; i < 200; ++i) {
        char* buf = (char*)alloca(4096);
        buf[0] = (char)i;
        buf[4095] = (char)i;
        if (i % 50 == 0)
            std::printf("  第 %3d 轮 alloca 缓冲 = %p\n", i, (void*)buf);
    }

    std::printf("[深递归] 每帧 ~192B,深度 12000,每 3000 层报一次帧地址:\n");
    struct Recur {
        static long descend(int depth) {
            volatile char pad[192];
            pad[0] = (char)depth;
            pad[191] = (char)depth;
            if (depth % 3000 == 0)
                std::printf("  depth %5d 帧内 pad = %p\n", depth, (void*)pad);
            long acc = pad[0] + pad[191];
            if (depth > 0)
                acc += descend(depth - 1);
            return acc + pad[0]; // 递归后仍读 pad,防止尾调用化/帧折叠
        }
    };
    long acc = Recur::descend(12000);
    Range stack_after = mapping_of((unsigned long)&acc, nullptr);
    std::printf("  递归返回后局部变量地址 = %p\n", (void*)&acc);
    std::printf("[stack] 现在 %lx-%lx:起始(低地址端)下探了 %luKB,顶端不动 → 向低地址生长\n",
                stack_after.start, stack_after.end,
                (stack_before.start - stack_after.start) / 1024);

    // ---------- E3-3 归还 ----------
    // ③a free 64MB 大块(E3-1 里最后一笔),mmap 映射应随之 munmap
    void* p64 = keep.back();
    unsigned long addr64 = (unsigned long)p64; // 存数值,free 后只用数值查映射
    Range m64 = mapping_of(addr64, nullptr);
    std::printf("\n== E3-3 归还观察 ==\n");
    std::printf("[a] free 前的 64MB 块:%p 所在映射 %lx-%lx(%luKB)\n", p64, m64.start, m64.end,
                (m64.end - m64.start) / 1024);
    std::free(p64);
    Range m64b = mapping_of(addr64, nullptr);
    std::printf("    free 后:所在映射 %s\n",
                m64b.found ? "仍存在" : "已消失(munmap 立刻归还地址空间)");

    // ③b 256×48KB 小块喂大 [heap] 再全 free:brk 收缩观察
    Range hb = heap_range();
    std::printf("[b] 256×48KB 全部来自 brk:喂大前 [heap]=%lx-%lx(%luKB)\n", hb.start, hb.end,
                (hb.end - hb.start) / 1024);
    std::vector<void*> small(256);
    for (auto& p : small) {
        p = std::malloc(48 * 1024);
        ((char*)p)[0] = 1;
    }
    Range hf = heap_range();
    std::printf("    喂大后 [heap]=%lx-%lx(%luKB,涨了 %luKB)\n", hf.start, hf.end,
                (hf.end - hf.start) / 1024, (hf.end - hb.end) / 1024);
    for (auto p : small)
        std::free(p);
    Range ha = heap_range();
    std::printf("    全部 free 后 [heap]=%lx-%lx(%luKB,%s)\n", ha.start, ha.end,
                (ha.end - ha.start) / 1024, ha.end < hf.end ? "brk 已收缩" : "没有收缩");
    std::printf(
        "    → 小块 free 后堆顶以上归还内核(留 M_TOP_PAD 常驻),但已 write 过的页不再还给内核\n");

    // ③c 动态阈值:释放一笔 4MB(mmap 来源)的块。glibc 规则:free 一块比当前阈值
    // 大的 mmap 块时,把 mmap 阈值抬到该块大小(动态调整,除非 mallopt 显式设过)。
    // 此后再 malloc,小于新阈值的请求就回到 [heap]。用 3MB / 5MB 两笔夹逼验证。
    void* p4 = keep[keep.size() - 2]; // 4194304 那笔
    std::printf("[c] free 一笔 4MB mmap 块(%p),让 glibc 抬高动态 mmap 阈值:\n", p4);
    std::free(p4);
    Range h1 = heap_range();
    void* p3m = std::malloc(3 << 20); // 3MB < 抬高后的阈值(≈4MB)→ 预期回 [heap]
    ((char*)p3m)[0] = 1;
    Range hit;
    const char* src3m = classify_ptr(p3m, &hit);
    Range h2 = heap_range();
    std::printf("    malloc(3MB) = %p → %s([heap] end %lx → %lx)\n", p3m, src3m, h1.end, h2.end);
    void* p5m = std::malloc(5 << 20); // 5MB > 抬高后的阈值 → 预期仍走 mmap
    ((char*)p5m)[0] = 1;
    const char* src5m = classify_ptr(p5m, &hit);
    std::printf("    malloc(5MB) = %p → %s\n", p5m, src5m);
    std::printf("    → 阈值被抬到被释放块的大小(≈4MB):3MB 回 brk、5MB 仍 mmap\n");
    return 0;
}
