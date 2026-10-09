// E1: 对齐分配三件套 —— aligned_alloc / posix_memalign / C++17 对齐 new
// 验证三者的返回地址都满足 addr % align == 0;对齐值非法时报错口径;
// glibc 2.38 起 aligned_alloc 不再要求 size 是 alignment 的倍数(C17 放宽,man 3 aligned_alloc
// VERSIONS),本机实测。
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <gnu/libc-version.h>
#include <memory_resource>
#include <new>

static void check(const char* who, void* p, size_t align) {
    if (p == nullptr) {
        std::printf("%-34s -> NULL\n", who);
        return;
    }
    std::uintptr_t a = reinterpret_cast<std::uintptr_t>(p);
    std::printf("%-34s -> %#14zx  %%%-4zu = %zu  [%s]\n", who, a, align, a % align,
                a % align == 0 ? "OK" : "MISALIGNED!");
}

int main() {
    std::printf("glibc = %s\n\n", gnu_get_libc_version());

    const size_t aligns[] = {64, 4096};
    const size_t sizes[] = {64, 100, 4096, 10000}; // 100/10000 故意不是 64 的倍数

    for (size_t align : aligns) {
        std::printf("==== align = %zu ====\n", align);
        for (size_t n : sizes) {
            char who[64];
            std::snprintf(who, sizeof who, "aligned_alloc(%zu, %5zu)", align, n);
            errno = 0;
            void* p1 = aligned_alloc(align, n);
            check(who, p1, align);
            free(p1);

            std::snprintf(who, sizeof who, "posix_memalign(&p, %zu, %5zu)", align, n);
            errno = 0;
            void* p2 = nullptr;
            int rc = posix_memalign(&p2, align, n);
            std::printf("%-34s -> rc=%d(%s)\n", who, rc, rc == 0 ? "0=success" : strerror(rc));
            check("  address", p2, align);
            free(p2);

            std::snprintf(who, sizeof who, "operator new(%5zu, align_val_t(%zu))", n, align);
            errno = 0;
            void* p3 = ::operator new(n, std::align_val_t{align});
            check(who, p3, align);
            ::operator delete(p3, std::align_val_t{align});
        }
        std::printf("\n");
    }

    // C++17 语义: new 一个 over-aligned 类型,编译器自动走对齐 new/delete
    struct alignas(64) OverAligned {
        double d[8];
        long tag;
    };
    OverAligned* oa = new OverAligned{{}, 42};
    check("new OverAligned (alignas(64))", oa, 64);
    delete oa;

    // std::pmr:allocate(bytes, alignment) 对齐超过默认 new 对齐(16)时同样转发到对齐 new
    void* pm = std::pmr::new_delete_resource()->allocate(1000, 4096);
    check("pmr::new_delete_resource", pm, 4096);
    std::pmr::new_delete_resource()->deallocate(pm, 1000, 4096);
    std::printf("\n");

    std::printf("==== 非法对齐值 ====\n");
    errno = 0;
    void* bad = aligned_alloc(48, 128); // 48 不是二次幂
    std::printf("aligned_alloc(48, 128)    -> %p errno=%d(%s)\n", bad, errno, strerror(errno));
    errno = 0;
    bad = aligned_alloc(0, 128); // 0 非法
    std::printf("aligned_alloc(0, 128)     -> %p errno=%d(%s)\n", bad, errno, strerror(errno));
    errno = 0;
    bad = aligned_alloc(3, 96); // 3 不是二次幂
    std::printf("aligned_alloc(3, 96)      -> %p errno=%d(%s)\n", bad, errno, strerror(errno));

    void* pm2 = (void*)0x1;
    int rc = posix_memalign(&pm2, 48, 128); // 48 不是二次幂 -> 返回 EINVAL(不是设 errno)
    std::printf("posix_memalign(&p,48,128) -> rc=%d(%s) p=%p\n", rc, strerror(rc), pm2);
    rc = posix_memalign(&pm2, 4, 128); // 4 < sizeof(void*)=8
    std::printf("posix_memalign(&p,4,128)  -> rc=%d(%s) p=%p\n", rc, strerror(rc), pm2);
    try {
        void* q = ::operator new(8, std::align_val_t{3}); // 对齐 new 非二次幂:glibc 层拦还是抛?
        std::printf("operator new(8, align_val_t{3}) -> 意外成功 %p(不该走到)\n", q);
        ::operator delete(q, std::align_val_t{3});
    } catch (const std::bad_alloc& e) {
        std::printf("operator new(8, align_val_t{3}) -> 抛 std::bad_alloc: %s\n", e.what());
    }
    errno = 0;
    void* ok3 = aligned_alloc(64, 100); // size 非 alignment 倍数:glibc 2.38+ 已放宽
    std::printf("aligned_alloc(64, 100)    -> %p errno=%d (size 非倍数,C17 放宽后 glibc 接受)\n",
                ok3, errno);
    free(ok3);
    return 0;
}
