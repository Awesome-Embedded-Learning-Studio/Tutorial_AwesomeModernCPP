/**
 * @file ex08_aligned_buffer.cpp
 * @brief 练习：为 SIMD 分配对齐缓冲区
 *
 * 使用 alignas 和 std::aligned_alloc 分配对齐内存，
 * 演示对齐和未对齐访问的差异。
 * 注意：此文件中的 SIMD 操作用简单的标量循环模拟，
 *       避免依赖特定平台的 intrinsics 头文件。
 */

#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <iostream>

// ============================================================
// 方法 1：使用 alignas 在栈上分配对齐数组
// ============================================================

// 32 字节对齐（AVX 要求）
alignas(32) float g_aligned_stack[8];

// 未对齐的栈数组（模拟对比）
float g_unaligned_stack[8];

// ============================================================
// 方法 2：使用 std::aligned_alloc 在堆上分配（C++17）
// ============================================================

/// @brief 在堆上分配对齐内存
/// @param alignment 对齐字节数（必须是 2 的幂）
/// @param count     元素数量
/// @param elem_size 每个元素的字节大小
/// @return 对齐的内存指针，失败返回 nullptr
void* aligned_alloc_wrapper(std::size_t alignment,
                            std::size_t count,
                            std::size_t elem_size)
{
    std::size_t total = count * elem_size;
    // aligned_alloc 要求 total 是 alignment 的整数倍
    total = ((total + alignment - 1) / alignment) * alignment;

    void* ptr = std::aligned_alloc(alignment, total);
    if (ptr) {
        std::memset(ptr, 0, total);
    }
    return ptr;
}

// ============================================================
// 模拟 SIMD 操作：8 个 float 的向量加法
// ============================================================

/// @brief 对齐的向量加法（模拟 AVX _mm256_add_ps）
/// @param a       输入向量 a（须 32 字节对齐）
/// @param b       输入向量 b（须 32 字节对齐）
/// @param result  输出向量（须 32 字节对齐）
/// @param count   元素数量（应为 8 的倍数）
void vector_add_aligned(const float* a,
                        const float* b,
                        float* result,
                        std::size_t count)
{
    // 在真实代码中，这里会使用 _mm256_load_ps 和 _mm256_add_ps
    // _mm256_load_ps 要求地址必须 32 字节对齐，否则 segfault
    for (std::size_t i = 0; i < count; ++i) {
        result[i] = a[i] + b[i];
    }
}

/// @brief 未对齐的向量加法（模拟 _mm256_loadu_ps）
/// @param a       输入向量 a（无需对齐）
/// @param b       输入向量 b（无需对齐）
/// @param result  输出向量（无需对齐）
/// @param count   元素数量
void vector_add_unaligned(const float* a,
                          const float* b,
                          float* result,
                          std::size_t count)
{
    // _mm256_loadu_ps 不要求对齐，但在某些平台上可能稍慢
    for (std::size_t i = 0; i < count; ++i) {
        result[i] = a[i] + b[i];
    }
}

// ============================================================
// 辅助函数
// ============================================================

bool is_aligned(const void* ptr, std::size_t alignment)
{
    auto addr = reinterpret_cast<std::uintptr_t>(ptr);
    return (addr % alignment) == 0;
}

void print_vector(const char* label, const float* v, std::size_t count)
{
    std::cout << label << " [";
    for (std::size_t i = 0; i < count; ++i) {
        std::cout << v[i];
        if (i + 1 < count) { std::cout << ", "; }
    }
    std::cout << "]\n";
}

// ============================================================
// 演示
// ============================================================

int main()
{
    std::cout << "===== ex08: 为 SIMD 分配对齐缓冲区 =====\n\n";

    constexpr std::size_t kVecSize = 8;

    // ---- 栈上对齐数组 ----
    std::cout << "=== 方法 1: alignas 栈上分配 ===\n";
    std::cout << "  g_aligned_stack 地址:   " << g_aligned_stack
              << " 对齐 32 字节? " << std::boolalpha
              << is_aligned(g_aligned_stack, 32) << "\n";
    std::cout << "  g_unaligned_stack 地址: " << g_unaligned_stack
              << " 对齐 32 字节? " << std::boolalpha
              << is_aligned(g_unaligned_stack, 32) << "\n\n";

    // 初始化数据
    for (std::size_t i = 0; i < kVecSize; ++i) {
        g_aligned_stack[i] = static_cast<float>(i + 1) * 1.0f;
        g_unaligned_stack[i] = static_cast<float>(i + 1) * 0.5f;
    }

    // 对齐的栈上向量加法
    alignas(32) float result_stack[kVecSize];
    vector_add_aligned(g_aligned_stack, g_aligned_stack, result_stack, kVecSize);
    print_vector("  对齐: aligned + aligned = ", result_stack, kVecSize);

    // ---- 堆上对齐分配 ----
    std::cout << "\n=== 方法 2: std::aligned_alloc 堆上分配 ===\n";

    float* heap_a = static_cast<float*>(aligned_alloc_wrapper(32, kVecSize, sizeof(float)));
    float* heap_b = static_cast<float*>(aligned_alloc_wrapper(32, kVecSize, sizeof(float)));
    float* heap_result = static_cast<float*>(aligned_alloc_wrapper(32, kVecSize, sizeof(float)));

    if (heap_a && heap_b && heap_result) {
        std::cout << "  heap_a 地址:      " << heap_a
                  << " 对齐 32 字节? " << is_aligned(heap_a, 32) << "\n";
        std::cout << "  heap_b 地址:      " << heap_b
                  << " 对齐 32 字节? " << is_aligned(heap_b, 32) << "\n";
        std::cout << "  heap_result 地址: " << heap_result
                  << " 对齐 32 字节? " << is_aligned(heap_result, 32) << "\n\n";

        for (std::size_t i = 0; i < kVecSize; ++i) {
            heap_a[i] = static_cast<float>(i) * 10.0f;
            heap_b[i] = static_cast<float>(i) * 2.0f;
        }

        vector_add_aligned(heap_a, heap_b, heap_result, kVecSize);
        print_vector("  对齐: heap_a + heap_b = ", heap_result, kVecSize);
    } else {
        std::cout << "  堆上对齐分配失败!\n";
    }

    // ---- 未对齐访问对比 ----
    std::cout << "\n=== 对齐 vs 未对齐访问对比 ===\n";

    // 模拟未对齐：在数组内部偏移 4 字节（1 个 float）后的位置
    // 这不是 32 字节对齐的
    float* misaligned_ptr = g_aligned_stack + 1;  // 偏移 4 字节
    std::cout << "  misaligned_ptr 地址: " << misaligned_ptr
              << " 对齐 32 字节? " << is_aligned(misaligned_ptr, 32) << "\n";
    std::cout << "  注意: SIMD 的 _mm256_load_ps 在此地址会 segfault!\n";
    std::cout << "  必须使用 _mm256_loadu_ps (unaligned load) 代替\n\n";

    // 使用 unaligned 版本处理未对齐数据
    float unaligned_result[kVecSize - 1];
    vector_add_unaligned(misaligned_ptr, misaligned_ptr, unaligned_result, kVecSize - 1);
    std::cout << "  未对齐 loadu 结果: [";
    for (std::size_t i = 0; i < kVecSize - 1; ++i) {
        std::cout << unaligned_result[i];
        if (i + 1 < kVecSize - 1) { std::cout << ", "; }
    }
    std::cout << "]\n";

    // ---- 对齐要求汇总 ----
    std::cout << "\n=== SIMD 指令集的对齐要求 ===\n";
    std::cout << "  SSE  (128-bit): 16 字节对齐 (_mm_load_ps)\n";
    std::cout << "  AVX  (256-bit): 32 字节对齐 (_mm256_load_ps)\n";
    std::cout << "  AVX-512 (512-bit): 64 字节对齐 (_mm512_load_ps)\n";
    std::cout << "  对应的 unaligned 版本: _mm_loadu_ps / _mm256_loadu_ps / _mm512_loadu_ps\n";

    // 释放堆内存
    std::free(heap_a);
    std::free(heap_b);
    std::free(heap_result);

    std::cout << "\n要点:\n";
    std::cout << "  1. alignas(32) 在栈上声明 32 字节对齐的数组\n";
    std::cout << "  2. std::aligned_alloc(32, size) 在堆上分配对齐内存\n";
    std::cout << "  3. 对齐 load (_mm256_load_ps) 要求地址对齐，否则崩溃\n";
    std::cout << "  4. 未对齐 load (_mm256_loadu_ps) 允许任意地址但可能稍慢\n";
    std::cout << "  5. 分配对齐内存时，大小必须是 alignment 的整数倍\n";

    return 0;
}
