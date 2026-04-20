/**
 * @file ex05_memory_pool.cpp
 * @brief 练习：用自定义删除器实现简单内存池
 *
 * 实现一个固定大小的内存池类，预分配一块大内存，
 * 用 unique_ptr 配合自定义删除器管理从池中分配的对象。
 * 对象析构时不调用 delete，而是归还给内存池。
 */

#include <cassert>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <memory>
#include <vector>

// ============================================================
// SimpleMemoryPool: 固定块大小的内存池
// ============================================================

class SimpleMemoryPool {
public:
    /// @brief 构造内存池
    /// @param block_size  每个块的大小（字节）
    /// @param block_count 块的数量
    SimpleMemoryPool(std::size_t block_size, std::size_t block_count)
        : block_size_(block_size)
        , block_count_(block_count)
        , buffer_(new char[block_size * block_count])
        , free_list_()
    {
        // 将所有块加入空闲链表
        for (std::size_t i = 0; i < block_count; ++i) {
            char* block = buffer_.get() + i * block_size;
            free_list_.push_back(block);
        }

        std::cout << "  MemoryPool: 分配 " << block_count << " 个块，"
                  << "每块 " << block_size << " 字节，"
                  << "总计 " << (block_size * block_count) << " 字节\n";
    }

    ~SimpleMemoryPool()
    {
        std::cout << "  MemoryPool: 销毁，回收 "
                  << (block_size_ * block_count_) << " 字节\n";
    }

    // 禁止拷贝和移动
    SimpleMemoryPool(const SimpleMemoryPool&) = delete;
    SimpleMemoryPool& operator=(const SimpleMemoryPool&) = delete;

    /// @brief 从池中分配一个块
    /// @return 块的地址，池满时返回 nullptr
    void* allocate()
    {
        if (free_list_.empty()) {
            std::cout << "  MemoryPool: 池已满，分配失败\n";
            return nullptr;
        }
        char* block = free_list_.back();
        free_list_.pop_back();
        std::cout << "  MemoryPool: 分配块 @ " << static_cast<void*>(block)
                  << " (剩余 " << free_list_.size() << " 块)\n";
        return block;
    }

    /// @brief 将块归还给池
    /// @param block 要归还的块的地址
    void deallocate(void* block)
    {
        if (!block) { return; }

        // 简单验证地址在池范围内
        char* ptr = static_cast<char*>(block);
        assert(ptr >= buffer_.get() &&
               ptr < buffer_.get() + block_size_ * block_count_);

        free_list_.push_back(ptr);
        std::cout << "  MemoryPool: 回收块 @ " << block
                  << " (剩余 " << free_list_.size() << " 块)\n";
    }

    /// @brief 查询剩余可用块数
    std::size_t available() const { return free_list_.size(); }

    /// @brief 查询块大小
    std::size_t block_size() const { return block_size_; }

private:
    std::size_t block_size_;
    std::size_t block_count_;
    std::unique_ptr<char[]> buffer_;
    std::vector<char*> free_list_;
};

// ============================================================
// 自定义删除器：将内存归还给池而非调用 delete
// ============================================================

/// @brief 通用的池归还删除器
/// @tparam T 对象类型
template <typename T>
class PoolDeleter {
public:
    explicit PoolDeleter(SimpleMemoryPool& pool) : pool_(&pool) {}

    void operator()(T* ptr)
    {
        if (ptr) {
            std::cout << "  PoolDeleter: 析构 " << ptr << "\n";
            ptr->~T();  // 显式调用析构函数（placement new 的对应操作）
            pool_->deallocate(ptr);
        }
    }

private:
    SimpleMemoryPool* pool_;
};

// unique_ptr 类型别名
template <typename T>
using PoolPtr = std::unique_ptr<T, PoolDeleter<T>>;

/// @brief 从内存池构造对象
/// @tparam T    对象类型
/// @tparam Args 构造函数参数类型
/// @param pool  内存池
/// @param args  构造函数参数
/// @return 管理该对象的 unique_ptr
template <typename T, typename... Args>
PoolPtr<T> make_pool_ptr(SimpleMemoryPool& pool, Args&&... args)
{
    void* memory = pool.allocate();
    if (!memory) {
        return PoolPtr<T>(nullptr, PoolDeleter<T>(pool));
    }
    // placement new: 在池分配的内存上构造对象
    T* obj = new (memory) T(std::forward<Args>(args)...);
    return PoolPtr<T>(obj, PoolDeleter<T>(pool));
}

// ============================================================
// 测试用的简单类
// ============================================================

class Task {
public:
    explicit Task(int id, const std::string& name)
        : id_(id), name_(name)
    {
        std::cout << "  Task(" << id_ << ", \"" << name_ << "\") 构造\n";
    }

    ~Task()
    {
        std::cout << "  Task(" << id_ << ", \"" << name_ << "\") 析构\n";
    }

    void execute() const
    {
        std::cout << "  执行 Task(" << id_ << "): " << name_ << "\n";
    }

private:
    int id_;
    std::string name_;
};

// ============================================================
// 演示
// ============================================================

int main()
{
    std::cout << "===== ex05: 用自定义删除器实现简单内存池 =====\n\n";

    // 创建内存池：每个块大小足够容纳 Task 对象，共 4 个块
    constexpr std::size_t kBlockSize = sizeof(Task);
    constexpr std::size_t kBlockCount = 4;

    SimpleMemoryPool pool(kBlockSize, kBlockCount);

    std::cout << "\n--- 从池中分配对象 ---\n";
    {
        // 从池中构造对象，用 unique_ptr + 自定义删除器管理
        auto task1 = make_pool_ptr<Task>(pool, 1, "初始化系统");
        auto task2 = make_pool_ptr<Task>(pool, 2, "加载数据");
        auto task3 = make_pool_ptr<Task>(pool, 3, "处理请求");

        std::cout << "  池剩余块数: " << pool.available() << "\n\n";

        task1->execute();
        task2->execute();
        task3->execute();

        std::cout << "\n--- 离开作用域，对象自动归还给池 ---\n";
        // task1, task2, task3 离开作用域
        // PoolDeleter 调用析构函数 + pool.deallocate()
    }

    std::cout << "\n  池剩余块数 (全部回收): " << pool.available() << "\n";

    std::cout << "\n--- 重新分配回收的块 ---\n";
    {
        auto task4 = make_pool_ptr<Task>(pool, 4, "生成报告");
        auto task5 = make_pool_ptr<Task>(pool, 5, "清理临时文件");

        std::cout << "  池剩余块数: " << pool.available() << "\n";

        task4->execute();
        task5->execute();

        std::cout << "\n--- 测试池满 ---\n";
        auto task6 = make_pool_ptr<Task>(pool, 6, "后台任务 A");
        auto task7 = make_pool_ptr<Task>(pool, 7, "后台任务 B");
        auto task8 = make_pool_ptr<Task>(pool, 8, "溢出任务");

        if (!task8) {
            std::cout << "  第 5 个对象分配失败 (池已满)，符合预期\n";
        }

        // 池满时的 task8 是空 unique_ptr，PoolDeleter 不会操作 nullptr
    }

    std::cout << "\n要点:\n";
    std::cout << "  1. 内存池预分配一大块内存，避免频繁调用 new/delete\n";
    std::cout << "  2. 自定义删除器让 unique_ptr 归还内存给池而非 delete\n";
    std::cout << "  3. placement new 在池的内存上构造对象\n";
    std::cout << "  4. 析构时先显式调用 ~T()，再 deallocate() 归还内存\n";
    std::cout << "  5. RAII 保证即使异常退出，对象也会正确析构并归还\n";

    return 0;
}
