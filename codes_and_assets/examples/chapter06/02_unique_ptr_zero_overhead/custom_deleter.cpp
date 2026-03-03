// std::unique_ptr 自定义删除器示例
// 演示如何使用自定义删除器管理非堆资源

#include <memory>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// ========== malloc/free 删除器 ==========
struct FreeDeleter {
    void operator()(void* p) noexcept {
        printf("[FreeDeleter] Freeing memory at %p\n", p);
        std::free(p);
    }
};

using MallocPtr = std::unique_ptr<char, FreeDeleter>;

void malloc_example() {
    printf("=== malloc/free Example ===\n");

    MallocPtr buf(static_cast<char*>(std::malloc(128)));
    if (buf) {
        std::strcpy(buf.get(), "Hello from malloc!");
        printf("Buffer content: %s\n", buf.get());
    }
    // 离开作用域时自动调用 free()
}

// ========== 函数指针删除器 ==========
void my_free_fn(void* p) {
    printf("[my_free_fn] Freeing memory\n");
    std::free(p);
}

void func_ptr_deleter_example() {
    printf("\n=== Function Pointer Deleter Example ===\n");

    // 注意：函数指针作为删除器会增加 unique_ptr 的大小
    using FuncPtrDeleter = std::unique_ptr<char, void(*)(void*)>;

    FuncPtrDeleter buf(static_cast<char*>(std::malloc(64)), my_free_fn);
    printf("Buffer allocated with function pointer deleter\n");

    printf("sizeof(unique_ptr<char, FreeDeleter>): %zu\n",
           sizeof(std::unique_ptr<char, FreeDeleter>));
    printf("sizeof(unique_ptr<char, void(*)(void*)>): %zu\n",
           sizeof(FuncPtrDeleter));
}

// ========== 文件句柄删除器 ==========
struct FileDeleter {
    void operator()(FILE* f) const noexcept {
        if (f) {
            printf("[FileDeleter] Closing file\n");
            std::fclose(f);
        }
    }
};

using FilePtr = std::unique_ptr<FILE, FileDeleter>;

void file_example() {
    printf("\n=== File Handle Example ===\n");

    // 打开临时文件
    FilePtr fp(std::fopen("/tmp/test.txt", "w"));
    if (fp) {
        std::fprintf(fp.get(), "Hello, embedded world!\n");
        printf("Written to file\n");
    }
    // 离开作用域时自动 fclose
}

// ========== 自定义内存池删除器 ==========
struct MemoryPool {
    static constexpr size_t POOL_SIZE = 1024;
    char buffer[POOL_SIZE];
    size_t offset = 0;

    void* allocate(size_t size) {
        if (offset + size > POOL_SIZE) {
            printf("[Pool] Out of memory!\n");
            return nullptr;
        }
        void* ptr = buffer + offset;
        offset += size;
        printf("[Pool] Allocated %zu bytes at %p\n", size, ptr);
        return ptr;
    }

    void deallocate(void* p) {
        printf("[Pool] Deallocated %p (simple pool, no actual free)\n", p);
        // 简单池实现，实际上不释放
    }
};

struct PoolDeleter {
    MemoryPool* pool;
    void operator()(void* p) noexcept {
        if (pool && p) {
            pool->deallocate(p);
        }
    }
};

template<typename T>
using PoolPtr = std::unique_ptr<T, PoolDeleter>;

void pool_example() {
    printf("\n=== Memory Pool Example ===\n");

    MemoryPool pool;

    // 使用 placement new 在池中分配
    void* mem = pool.allocate(sizeof(int));
    int* ptr = new(mem) int(42);

    PoolPtr<int> pooled(ptr, PoolDeleter{&pool});
    printf("Value from pool: %d\n", *pooled);
}

// ========== 数组 unique_ptr ==========
void array_example() {
    printf("\n=== Array unique_ptr Example ===\n");

    auto arr = std::make_unique<int[]>(10);
    for (int i = 0; i < 10; ++i) {
        arr[i] = i * i;
    }

    printf("Array contents: ");
    for (int i = 0; i < 10; ++i) {
        printf("%d ", arr[i]);
    }
    printf("\n");

    // 离开作用域时自动调用 delete[]
}

int main() {
    malloc_example();
    func_ptr_deleter_example();
    file_example();
    pool_example();
    array_example();

    printf("\n=== All Examples Complete ===\n");

    return 0;
}
