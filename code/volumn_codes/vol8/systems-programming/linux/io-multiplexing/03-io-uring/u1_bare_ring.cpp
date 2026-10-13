// E1: 不用 liburing, 裸系统调用把环建起来
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <linux/io_uring.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

// x86-64 编号: io_uring_setup=425, io_uring_enter=426, io_uring_register=427
static unsigned *sq_head_p, *sq_tail_p, *sq_array;
static unsigned *cq_head_p, *cq_tail_p;
static io_uring_cqe* cqes;
static io_uring_sqe* sqes;
static unsigned sq_mask, cq_mask;

#define READ_ONCE(x) __atomic_load_n(&(x), __ATOMIC_ACQUIRE)
#define WRITE_ONCE(x, v) __atomic_store_n(&(x), (v), __ATOMIC_RELEASE)

static void dump_maps_for(void* p1, void* p2) {
    FILE* f = fopen("/proc/self/maps", "r");
    char line[512];
    while (fgets(line, sizeof line, f)) {
        unsigned long a1, a2;
        if (sscanf(line, "%lx-%lx", &a1, &a2) != 2)
            continue;
        unsigned long p = (unsigned long)p1, q = (unsigned long)p2;
        if ((a1 <= p && p < a2) || (a1 <= q && q < a2))
            std::printf("  %s", line);
    }
    fclose(f);
}

int main() {
    std::printf(
        "syscall 编号: __NR_io_uring_setup=%d __NR_io_uring_enter=%d __NR_io_uring_register=%d\n",
        (int)__NR_io_uring_setup, (int)__NR_io_uring_enter, (int)__NR_io_uring_register);

    io_uring_params p{};
    int fd = (int)syscall(__NR_io_uring_setup, 4, &p);
    if (fd < 0) {
        std::perror("io_uring_setup");
        return 1;
    }
    std::printf("io_uring_setup(4) = %d, sq_entries=%u cq_entries=%u features=0x%x\n", fd,
                p.sq_entries, p.cq_entries, p.features);
    std::printf("features: SINGLE_MMAP=%d NODROP=%d SUBMIT_STABLE=%d\n",
                !!(p.features & IORING_FEAT_SINGLE_MMAP), !!(p.features & IORING_FEAT_NODROP),
                !!(p.features & IORING_FEAT_SUBMIT_STABLE));

    // 三段 mmap: SQ 环, CQ 环, SQE 数组 (SINGLE_MMAP 时前两段是同一块)
    size_t sq_sz = p.sq_off.array + p.sq_entries * sizeof(unsigned);
    size_t cq_sz = p.cq_off.cqes + p.cq_entries * sizeof(io_uring_cqe);
    void* sq_map = mmap(nullptr, sq_sz, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, fd,
                        IORING_OFF_SQ_RING);
    void* cq_map = mmap(nullptr, cq_sz, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, fd,
                        IORING_OFF_CQ_RING);
    void* sqe_map = mmap(nullptr, p.sq_entries * sizeof(io_uring_sqe), PROT_READ | PROT_WRITE,
                         MAP_SHARED | MAP_POPULATE, fd, IORING_OFF_SQES);
    std::printf("mmap: SQ 环 %zu B @ %p, CQ 环 %zu B @ %p, SQE 数组 %zu B @ %p\n", sq_sz, sq_map,
                cq_sz, cq_map, p.sq_entries * sizeof(io_uring_sqe), sqe_map);
    std::printf("SINGLE_MMAP 置位: SQ/CQ 在同一块区域, 允许一次 mmap 同拿两环\n");
    std::printf("(这里按经典三段式分开映射, 地址自然不同: %p / %p; 特性省的是映射次数)\n", sq_map,
                cq_map);
    std::printf("/proc/self/maps 里能看到这两段映射:\n");
    dump_maps_for(sq_map, sqe_map);

    sq_head_p = (unsigned*)((char*)sq_map + p.sq_off.head);
    sq_tail_p = (unsigned*)((char*)sq_map + p.sq_off.tail);
    sq_array = (unsigned*)((char*)sq_map + p.sq_off.array);
    sq_mask = *(unsigned*)((char*)sq_map + p.sq_off.ring_mask);
    cq_head_p = (unsigned*)((char*)cq_map + p.cq_off.head);
    cq_tail_p = (unsigned*)((char*)cq_map + p.cq_off.tail);
    cqes = (io_uring_cqe*)((char*)cq_map + p.cq_off.cqes);
    cq_mask = *(unsigned*)((char*)cq_map + p.cq_off.ring_mask);
    sqes = (io_uring_sqe*)sqe_map;
    std::printf("环参数: sq_mask=%u cq_mask=%u (容量-1), SQE=%zu B, CQE=%zu B\n", sq_mask, cq_mask,
                sizeof(io_uring_sqe), sizeof(io_uring_cqe));

    // 手工塞一个 NOP 进 SQ
    unsigned tail = READ_ONCE(*sq_tail_p);
    io_uring_sqe* sqe = &sqes[tail & sq_mask];
    std::memset(sqe, 0, sizeof(*sqe));
    sqe->opcode = IORING_OP_NOP;
    sqe->user_data = 0xC0FFEE;
    sq_array[tail & sq_mask] = tail & sq_mask;
    WRITE_ONCE(*sq_tail_p, tail + 1); // 尾指针一放, 内核就看得见了
    std::printf("\n塞入 1 个 NOP (user_data=0xC0FFEE), SQ tail %u -> %u\n", tail, tail + 1);

    // 一次 enter: 提交 1 个, 顺带等 1 个完成
    long r = syscall(__NR_io_uring_enter, fd, 1, 1, IORING_ENTER_GETEVENTS, nullptr);
    std::printf("io_uring_enter(submit=1, wait=1, GETEVENTS) = %ld\n", r);

    // 从 CQ 里收
    unsigned head = READ_ONCE(*cq_head_p), ctail = READ_ONCE(*cq_tail_p);
    std::printf("CQ: head=%u tail=%u, 就绪 %u 个\n", head, ctail, ctail - head);
    while (head != ctail) {
        io_uring_cqe* cqe = &cqes[head & cq_mask];
        std::printf("  CQE: user_data=0x%llx res=%d flags=0x%x\n",
                    (unsigned long long)cqe->user_data, cqe->res, cqe->flags);
        WRITE_ONCE(*cq_head_p, ++head);
    }
    std::printf(
        "收完, CQ head=%u tail=%u。提交走 SQ 尾指针, 完成走 CQ: 两个环都是与内核共享的内存。\n",
        READ_ONCE(*cq_head_p), READ_ONCE(*cq_tail_p));
    return 0;
}
