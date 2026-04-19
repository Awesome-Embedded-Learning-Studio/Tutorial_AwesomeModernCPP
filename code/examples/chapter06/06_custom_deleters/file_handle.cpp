// 自定义删除器示例 - FILE* 和文件描述符
// 演示如何使用自定义删除器管理 C 风格的资源

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <memory>
#include <unistd.h>
#include <fcntl.h>

// ========== FILE* 删除器 ==========

struct FileDeleter {
    void operator()(FILE* f) const noexcept {
        if (f) {
            std::fclose(f);
            printf("[FileDeleter] File closed\n");
        }
    }
};

using FilePtr = std::unique_ptr<FILE, FileDeleter>;

FilePtr open_file(const char* path, const char* mode) {
    FILE* f = std::fopen(path, mode);
    if (!f) {
        printf("Failed to open %s\n", path);
        return FilePtr(nullptr);
    }
    printf("[open_file] Opened %s\n", path);
    return FilePtr(f);
}

void file_example() {
    printf("=== FILE* Example ===\n");

    auto fp = open_file("/tmp/test.txt", "w");
    if (fp) {
        std::fprintf(fp.get(), "Hello from embedded C++!\n");
        printf("Data written\n");
    }
    // 离开作用域时自动 fclose
}

// ========== 使用函数指针作为删除器 ==========

void my_free(void* p) noexcept {
    printf("[my_free] Freeing %p\n", p);
    std::free(p);
}

void func_ptr_deleter_example() {
    printf("\n=== Function Pointer Deleter Example ===\n");

    // 函数指针作为删除器
    using MallocPtr = std::unique_ptr<char, void(*)(void*)>;

    MallocPtr buf(static_cast<char*>(std::malloc(128)), my_free);
    if (buf) {
        std::strcpy(buf.get(), "Hello, malloc!");
        printf("Buffer: %s\n", buf.get());
    }

    // 比较大小
    printf("sizeof(unique_ptr<char, FileDeleter>): %zu\n",
           sizeof(std::unique_ptr<char, FileDeleter>));
    printf("sizeof(unique_ptr<char, void(*)(void*)>): %zu\n",
           sizeof(MallocPtr));
}

// ========== 无状态函数对象（零开销）==========

struct FreeDeleter {
    void operator()(void* p) noexcept {
        std::free(p);
    }
};

void stateless_deleter_example() {
    printf("\n=== Stateless Functor Deleter Example ===\n");

    using FreePtr = std::unique_ptr<char, FreeDeleter>;

    FreePtr buf(static_cast<char*>(std::malloc(64)));
    std::strcpy(buf.get(), "Zero overhead deleter");

    printf("Buffer: %s\n", buf.get());
    printf("sizeof(FreePtr): %zu (same as raw pointer: %zu)\n",
           sizeof(FreePtr), sizeof(char*));
}

// ========== 文件描述符删除器 ==========

struct FdDeleter {
    void operator()(int* fd) const noexcept {
        if (fd && *fd >= 0) {
            ::close(*fd);
            printf("[FdDeleter] Closed fd %d\n", *fd);
            delete fd;
        }
    }
};

using FdPtr = std::unique_ptr<int, FdDeleter>;

FdPtr open_fd(const char* path, int flags) {
    int fd = ::open(path, flags);
    if (fd < 0) {
        printf("Failed to open %s\n", path);
        return FdPtr(nullptr);
    }
    printf("[open_fd] Opened %s, fd=%d\n", path, fd);
    return FdPtr(new int(fd));
}

void fd_example() {
    printf("\n=== File Descriptor Example ===\n");

    auto fd = open_fd("/tmp/fd_test.txt", O_RDWR | O_CREAT);
    if (fd) {
        const char* msg = "Writing via fd\n";
        ::write(*fd, msg, 16);
        printf("Data written via fd %d\n", *fd);
    }
    // 离开作用域时自动 close
}

// ========== 有状态删除器（需要上下文）==========

struct DmaController {
    void release_buffer(void* p) {
        printf("[DmaController] Releasing buffer %p\n", p);
        // 实际的 DMA 释放逻辑
    }
};

struct DmaDeleter {
    DmaController* ctrl;

    void operator()(void* p) noexcept {
        if (ctrl && p) {
            ctrl->release_buffer(p);
        }
    }
};

template<typename T>
using DmaPtr = std::unique_ptr<T, DmaDeleter>;

void stateful_deleter_example() {
    printf("\n=== Stateful Deleter Example ===\n");

    DmaController controller;
    DmaPtr<uint8_t> buf(new uint8_t[256], DmaDeleter{&controller});

    printf("DMA buffer allocated\n");
    printf("sizeof(DmaPtr<uint8_t>): %zu (larger due to state)\n",
           sizeof(DmaPtr<uint8_t>));
}

// ========== 自定义 RAII 包装器（更轻量的替代方案）==========

struct FileDescriptor {
    int fd{-1};

    explicit FileDescriptor(int fd_) noexcept : fd(fd_) {}
    ~FileDescriptor() noexcept {
        if (fd >= 0) {
            ::close(fd);
            printf("[FileDescriptor] Closed fd %d\n", fd);
        }
    }

    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;

    FileDescriptor(FileDescriptor&& other) noexcept : fd(other.fd) {
        other.fd = -1;
    }

    FileDescriptor& operator=(FileDescriptor&& other) noexcept {
        if (this != &other) {
            if (fd >= 0) ::close(fd);
            fd = other.fd;
            other.fd = -1;
        }
        return *this;
    }

    int get() const noexcept { return fd; }
};

void custom_wrapper_example() {
    printf("\n=== Custom RAII Wrapper Example ===\n");

    FileDescriptor fd(::open("/tmp/wrapper_test.txt", O_RDWR | O_CREAT));
    if (fd.get() >= 0) {
        const char* msg = "Via custom wrapper\n";
        ::write(fd.get(), msg, 20);
        printf("Written via custom wrapper, fd=%d\n", fd.get());
    }
}

int main() {
    file_example();
    func_ptr_deleter_example();
    stateless_deleter_example();
    fd_example();
    stateful_deleter_example();
    custom_wrapper_example();

    printf("\n=== All Examples Complete ===\n");

    return 0;
}
