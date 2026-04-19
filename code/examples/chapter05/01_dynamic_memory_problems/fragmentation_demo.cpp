#include <iostream>
#include <cstdlib>
#include <vector>
#include <random>
#include <iomanip>

// 演示内存碎片化问题

class AllocationTracker {
    static size_t total_allocations;
    static size_t total_deallocations;
    static size_t current_bytes;
    static size_t peak_bytes;

public:
    static void* allocate(size_t size) {
        void* ptr = malloc(size);
        if (ptr) {
            total_allocations++;
            current_bytes += size;
            if (current_bytes > peak_bytes) {
                peak_bytes = current_bytes;
            }
        }
        return ptr;
    }

    static void deallocate(void* ptr, size_t size) {
        if (ptr) {
            total_deallocations++;
            current_bytes -= size;
            free(ptr);
        }
    }

    static void print_stats() {
        std::cout << "=== Allocation Statistics ===\n";
        std::cout << "Total allocations: " << total_allocations << "\n";
        std::cout << "Total deallocations: " << total_deallocations << "\n";
        std::cout << "Current bytes: " << current_bytes << "\n";
        std::cout << "Peak bytes: " << peak_bytes << "\n";
        std::cout << "============================\n";
    }
};

size_t AllocationTracker::total_allocations = 0;
size_t AllocationTracker::total_deallocations = 0;
size_t AllocationTracker::current_bytes = 0;
size_t AllocationTracker::peak_bytes = 0;

void fragmentation_scenario_1() {
    std::cout << "\n--- Scenario 1: Mixed Sizes ---\n";
    std::vector<void*> allocations;

    // 分配不同大小的块
    size_t sizes[] = {16, 32, 64, 128, 256, 512, 1024};

    for (int round = 0; round < 3; ++round) {
        for (size_t size : sizes) {
            void* ptr = AllocationTracker::allocate(size);
            if (ptr) {
                allocations.push_back(ptr);
                std::cout << "Allocated " << size << " bytes at " << ptr << "\n";
            }
        }
    }

    AllocationTracker::print_stats();

    // 释放一些（不是全部）- 制造外部碎片
    std::cout << "\nFreeing every other allocation...\n";
    for (size_t i = 1; i < allocations.size(); i += 2) {
        // 估算大小用于统计（实际应用中需要跟踪）
        size_t idx = (i % 7);
        size_t sizes_arr[] = {16, 32, 64, 128, 256, 512, 1024};
        size_t size = sizes_arr[idx];
        AllocationTracker::deallocate(allocations[i], size);
        std::cout << "Freed at " << allocations[i] << "\n";
    }

    AllocationTracker::print_stats();

    // 清理剩余
    for (size_t i = 0; i < allocations.size(); ++i) {
        if (i % 2 == 0 && allocations[i]) {
            size_t idx = (i % 7);
            size_t sizes_arr[] = {16, 32, 64, 128, 256, 512, 1024};
            size_t size = sizes_arr[idx];
            AllocationTracker::deallocate(allocations[i], size);
        }
    }
}

void fragmentation_scenario_2() {
    std::cout << "\n--- Scenario 2: Allocation/Deallocation Pattern ---\n";
    std::vector<void*> pool;

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> size_dist(64, 512);
    std::uniform_int_distribution<> action_dist(0, 1);

    for (int i = 0; i < 50; ++i) {
        if (action_dist(gen) == 0 || pool.empty()) {
            // 分配
            size_t size = size_dist(gen);
            void* ptr = malloc(size);
            if (ptr) {
                pool.push_back(ptr);
                std::cout << "Alloc " << size << " bytes at " << ptr << "\n";
            }
        } else {
            // 释放随机一个
            size_t idx = std::uniform_int_distribution<>(0, static_cast<int>(pool.size()) - 1)(gen);
            void* ptr = pool[idx];
            pool.erase(pool.begin() + idx);
            free(ptr);
            std::cout << "Freed at " << ptr << "\n";
        }
    }

    // 清理
    for (void* ptr : pool) {
        free(ptr);
    }

    std::cout << "Pool size: " << pool.size() << " allocations remaining\n";
}

struct Block {
    void* ptr;
    size_t size;
};

void fragmentation_scenario_3() {
    std::cout << "\n--- Scenario 3: Internal Fragmentation ---\n";

    // 假设分配器以16字节为粒度
    constexpr size_t granularity = 16;

    struct Alloc {
        size_t requested;
        size_t actual;
    };

    Alloc allocs[] = {
        {4, 16},   // 请求4字节，实际分配16字节（12字节内部碎片）
        {20, 32},  // 请求20字节，实际分配32字节（12字节内部碎片）
        {33, 48},  // 请求33字节，实际分配48字节（15字节内部碎片）
        {65, 80},  // 请求65字节，实际分配80字节（15字节内部碎片）
    };

    size_t total_requested = 0;
    size_t total_actual = 0;
    size_t total_internal_fragmentation = 0;

    for (const auto& alloc : allocs) {
        total_requested += alloc.requested;
        total_actual += alloc.actual;
        size_t frag = alloc.actual - alloc.requested;
        total_internal_fragmentation += frag;
        std::cout << "Requested: " << std::setw(3) << alloc.requested
                  << " bytes, Allocated: " << std::setw(3) << alloc.actual
                  << " bytes, Wasted: " << std::setw(3) << frag << " bytes\n";
    }

    std::cout << "\nTotal requested: " << total_requested << " bytes\n";
    std::cout << "Total allocated: " << total_actual << " bytes\n";
    std::cout << "Internal fragmentation: " << total_internal_fragmentation << " bytes\n";
    std::cout << "Efficiency: " << (100.0 * total_requested / total_actual) << "%\n";
}

int main() {
    std::cout << "=== Memory Fragmentation Demo ===\n";

    fragmentation_scenario_1();
    fragmentation_scenario_2();
    fragmentation_scenario_3();

    std::cout << "\n=== Key Takeaways ===\n";
    std::cout << "1. Mixed allocation sizes cause external fragmentation\n";
    std::cout << "2. Frequent alloc/dealloc patterns make fragmentation worse\n";
    std::cout << "3. Internal fragmentation wastes memory due to alignment/rounding\n";
    std::cout << "4. Use fixed-size pools or arena allocators to avoid fragmentation\n";

    return 0;
}
