// e4_spsc_ring.cpp —— E4 招牌:页文件后备共享内存里的 SPSC 环形队列,两进程 100 万条
//
// 编译(E4 是吞吐基准,加 -O2;WSL 里以相对路径调 MSYS2 UCRT64 g++):
//   /mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -O2 -Wall -Wextra e4_spsc_ring.cpp -o
//   e4_spsc_ring.exe
// 运行(三种通知模式各跑;父进程自动拉起消费者子进程,一条命令一轮):
//   chmod +x e4_spsc_ring.exe
//   ./e4_spsc_ring.exe spin      # 双方纯自旋(上界参照)
//   ./e4_spsc_ring.exe hybrid    # 自旋为主,空了才靠命名事件睡(实用形态)
//   ./e4_spsc_ring.exe notify    # 每条消息 SetEvent 一次(通知开销的下界参照)
//   ./e4_spsc_ring.exe burst     # 生产者每 1 万条歇 5ms,逼消费者真走睡眠-唤醒路径
//
// 结构(全部在共享内存里,两进程只认偏移):
//   Ring 头:capacity/mask/N + tail(生产者 release 发布)+ head(消费者 release 发布)
//   —— tail/head 各占一行缓存行,避免伪共享;consumer_sleeping 单独一行
//   槽数组:4096 槽 × 16B{value, tag},value=序号 i,tag=mix(i),消费侧逐条核对,
//   丢一条/乱一条都会立刻暴露
// 同步协议:
//   数据可见性靠 tail/head 的 acquire/release(Lamport SPSC,无 CAS);
//   hybrid 的"睡眠不丢唤醒":消费者 store(1,seq_cst) 后复查 tail,生产者发布 tail 后
//   atomic_thread_fence(seq_cst) 再查睡眠标志——两边至少一侧看得到对方
// 观察点:
//   (1) 100 万条消费侧核对:期望 0 错(值与 tag 逐条全对 = 无丢失无乱序)
//   (2) 三模式吞吐差:spin 是内存速度上界;notify 每条两次内核过渡(SetEvent+Wait),
//       掉一个数量级是正常的——这正是"事件只该兜底,不该逐条"的实证
//   (3) hybrid 的睡眠次数:消费者跟得上就整轮 0 次睡眠,兜底通道闲着,这才是实用形态
//   (4) 生产侧"遇满自旋"次数:消费者慢于生产者时环形缓冲的背压就体现在这

#include "../common/shm_util.hpp"
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <immintrin.h>
#include <new>

static constexpr uint64_t kMsgCount = 1'000'000;
static constexpr uint64_t kCap = 4096;        // 2 的幂
static constexpr uint32_t kSlotsOff = 0x1000; // 槽数组在对象内的偏移(协议只认偏移)
static constexpr uint32_t kShmSize = 0x20000; // 128KB

struct alignas(64) RingHead { // 共享内存里的队列头(首 4KB)
    uint64_t magic;
    uint64_t nmsgs;
    uint64_t capacity;
    uint64_t mask;
    std::atomic<uint64_t> tail; // 生产者写(consumer 读)
    char pad0[64 - sizeof(std::atomic<uint64_t>)];
    std::atomic<uint64_t> head; // 消费者写(producer 读)
    char pad1[64 - sizeof(std::atomic<uint64_t>)];
    std::atomic<uint64_t> sleeping; // hybrid:消费者要睡了
    // 统计(消费者写,生产者最后读)
    uint64_t cons_ns;
    uint64_t cons_errors;
    uint64_t cons_sleeps;
};
struct Slot { // 16B 消息
    uint64_t value;
    uint64_t tag;
};

static uint64_t mix(uint64_t v) {
    v *= 0x9E3779B97F4A7C15ull;
    return v ^ (v >> 31);
}

// ---------------- 消费者:argv = e4_spsc_ring.exe consumer <父pid> <mode> ----------------
static int run_consumer(DWORD ppid, const char* mode) {
    HANDLE hm = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, local_name(L"E4", ppid).c_str());
    unsigned char* base = (unsigned char*)MapViewOfFile(hm, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    auto* rh = (RingHead*)base;
    Slot* slots = (Slot*)(base + kSlotsOff);
    HANDLE go = OpenEventW(SYNCHRONIZE, FALSE, local_name(L"E4go", ppid).c_str());
    HANDLE ready = OpenEventW(EVENT_MODIFY_STATE, FALSE, local_name(L"E4r", ppid).c_str());
    HANDLE data =
        OpenEventW(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, local_name(L"E4data", ppid).c_str());
    SetEvent(ready);
    WaitForSingleObject(go, INFINITE); // 统一起跑

    const bool m_notify = strcmp(mode, "notify") == 0;
    const bool m_spin = strcmp(mode, "spin") == 0;
    uint64_t expected = 0, errors = 0, sleeps = 0;
    uint64_t h = 0; // head 缓存
    int64_t t_start = qpc_ns();
    while (expected < rh->nmsgs) {
        uint64_t t = rh->tail.load(std::memory_order_acquire);
        if (h == t) { // 空
            if (m_spin) {
                _mm_pause();
                continue;
            }
            if (m_notify) {
                WaitForSingleObject(data, INFINITE);
                ++sleeps;
                continue;
            }
            // hybrid:先自旋一阵,还空才睡;睡前立牌位,防错过唤醒
            for (int i = 0; i < 20000 && h == t; ++i) {
                _mm_pause();
                t = rh->tail.load(std::memory_order_acquire);
            }
            if (h == t) {
                rh->sleeping.store(1, std::memory_order_seq_cst);
                t = rh->tail.load(std::memory_order_acquire);
                if (h == t) {
                    WaitForSingleObject(data, INFINITE);
                    ++sleeps;
                }
                rh->sleeping.store(0, std::memory_order_seq_cst);
            }
            continue;
        }
        do { // 一次醒来能取多少取多少(事件是"有数据"信号,不是数据本身)
            Slot& s = slots[h & rh->mask];
            if (s.value != expected || s.tag != mix(s.value)) {
                ++errors;
            }
            ++expected;
            h = h + 1;
            rh->head.store(h, std::memory_order_release);
            t = rh->tail.load(std::memory_order_acquire);
        } while (h != t && expected < rh->nmsgs);
    }
    int64_t t_end = qpc_ns();
    rh->cons_ns = (uint64_t)(t_end - t_start);
    rh->cons_errors = errors;
    rh->cons_sleeps = sleeps;
    UnmapViewOfFile(base);
    CloseHandle(hm);
    CloseHandle(go);
    CloseHandle(ready);
    CloseHandle(data);
    return errors == 0 ? 0 : 2;
}

// ---------------- 生产者 = 父进程 ----------------
int main(int argc, char** argv) {
    if (argc == 4 && strcmp(argv[1], "consumer") == 0) {
        return run_consumer((DWORD)strtoul(argv[2], nullptr, 10), argv[3]);
    }
    const char* mode = argc >= 2 ? argv[1] : "hybrid";
    const bool m_notify = strcmp(mode, "notify") == 0;
    const bool m_spin = strcmp(mode, "spin") == 0;
    const bool m_burst = strcmp(mode, "burst") == 0;
    if (!m_notify && !m_spin && !m_burst && strcmp(mode, "hybrid") != 0) {
        printf("用法:e4_spsc_ring.exe spin|hybrid|notify|burst\n");
        return 1;
    }
    const DWORD pid = GetCurrentProcessId();
    HANDLE hm = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, kShmSize,
                                   local_name(L"E4", pid).c_str());
    unsigned char* base = (unsigned char*)MapViewOfFile(hm, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    auto* rh = new (base) RingHead{}; // 全零页上重建(lock-free 原子,零值合法)
    rh->magic = 0x53505343ull;        // 'SPSC'
    rh->nmsgs = kMsgCount;
    rh->capacity = kCap;
    rh->mask = kCap - 1;
    Slot* slots = (Slot*)(base + kSlotsOff);
    HANDLE go = CreateEventW(nullptr, TRUE, FALSE, local_name(L"E4go", pid).c_str());
    HANDLE ready = CreateEventW(nullptr, FALSE, FALSE, local_name(L"E4r", pid).c_str());
    HANDLE data =
        CreateEventW(nullptr, FALSE, FALSE, local_name(L"E4data", pid).c_str()); // 自动复位

    printf("[E4] 模式=%s 消息数=%llu 槽数=%llu(16B/槽) 对象=%ls\n", mode,
           (unsigned long long)kMsgCount, (unsigned long long)kCap, local_name(L"E4", pid).c_str());

    // 双方钉不同核(9700X 同 CCD),消除迁移噪声
    std::wstring wmode(mode, mode + strlen(mode));
    PROCESS_INFORMATION pi{};
    if (!spawn_self((L"consumer " + std::to_wstring(pid) + L" " + wmode).c_str(), pi)) {
        printf("拉消费者失败 err=%lu\n", GetLastError());
        return 1;
    }
    SetProcessAffinityMask(GetCurrentProcess(), 1 << 2);
    SetProcessAffinityMask(pi.hProcess, 1 << 3);
    WaitForSingleObject(ready, INFINITE);

    uint64_t t_cache = 0; // 生产者的 head 缓存
    uint64_t full_spins = 0, notifies = 0;
    uint64_t tail = 0;
    SetEvent(go);
    int64_t t_start = qpc_ns();
    for (uint64_t i = 0; i < kMsgCount; ++i) {
        while (tail - t_cache == kCap) { // 满:背压,等消费者腾地方
            t_cache = rh->head.load(std::memory_order_acquire);
            ++full_spins;
            _mm_pause();
        }
        if (m_burst && i % 10000 == 0 && i > 0) {
            Sleep(5);
        } // 逼消费者睡着,验唤醒协议
        Slot& s = slots[tail & rh->mask];
        s.value = i;
        s.tag = mix(i);
        tail = tail + 1;
        rh->tail.store(tail, std::memory_order_release);
        if (m_notify) {
            SetEvent(data);
            ++notifies;
        } else if (!m_spin) { // hybrid:发布后查睡眠牌位,立了才通知
            std::atomic_thread_fence(std::memory_order_seq_cst);
            if (rh->sleeping.load(std::memory_order_relaxed)) {
                SetEvent(data);
                ++notifies;
            }
        }
    }
    int64_t t_end = qpc_ns();
    uint64_t prod_ns = (uint64_t)(t_end - t_start);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD ccode = 0;
    GetExitCodeProcess(pi.hProcess, &ccode);

    double prod_ms = prod_ns / 1e6, cons_ms = (double)rh->cons_ns / 1e6;
    double prod_mps = (double)kMsgCount / (double)prod_ns * 1e9; // msg/s
    double cons_mps = (double)kMsgCount / (double)rh->cons_ns * 1e9;
    printf("[生产者 pid=%lu cpu2] %llu 条,%.2f ms,吞吐 %.0f 万条/s(%.2f ns/条),遇满自旋 %llu 次\n",
           pid, (unsigned long long)kMsgCount, prod_ms, prod_mps / 1e4, prod_ns / (double)kMsgCount,
           (unsigned long long)full_spins);
    printf("[消费者 退出码=%lu] %llu 条,核对错误 %llu 条(值+tag 逐条验),%.2f ms,吞吐 %.0f "
           "万条/s,%s %llu 次\n",
           ccode, (unsigned long long)kMsgCount, (unsigned long long)rh->cons_errors, cons_ms,
           cons_mps / 1e4, m_notify ? "等待" : "睡眠", (unsigned long long)rh->cons_sleeps);
    printf("[E4] %s:SetEvent 共 %llu 次;%s\n", mode, (unsigned long long)notifies,
           rh->cons_errors == 0 ? "100 万条零丢失零乱序" : "!! 有差错,查协议");
    fflush(stdout);
    UnmapViewOfFile(base);
    CloseHandle(hm);
    CloseHandle(go);
    CloseHandle(ready);
    CloseHandle(data);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
}
