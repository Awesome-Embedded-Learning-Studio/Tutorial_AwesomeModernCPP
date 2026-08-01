// 配套 05-type-safe-any.md「小对象优化 SBO」
// 小类型直接存在 any 内部 buffer,大类型才堆分配;if constexpr 在 clone 时选路径
// 用全局 operator new 计数,直观对比堆分配次数
// 编译运行:g++ -std=c++17 -Wall -Wextra sbo_any.cpp -o sbo && ./sbo
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <new>
#include <type_traits>
#include <typeinfo>
#include <utility>

static int g_alloc_count = 0;
void* operator new(std::size_t n) {
    ++g_alloc_count;
    return std::malloc(n);
}
void operator delete(void* p) noexcept {
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept {
    std::free(p);
}

class SboAny {
  public:
    static constexpr std::size_t BUF = sizeof(void*) * 3; // 24 字节(64 位)

  private:
    struct concept_base {
        virtual ~concept_base() {}
        virtual const std::type_info& type() const noexcept = 0;
        // 能塞进 buf 就 placement new 塞进去,返回指针;塞不下返回 nullptr
        virtual concept_base* clone_into(char* buf) const = 0;
        virtual concept_base* clone_heap() const = 0; // 堆上克隆
    };

    template <typename T> struct model final : concept_base {
        T data;
        template <typename... A> explicit model(A&&... a) : data(std::forward<A>(a)...) {}
        const std::type_info& type() const noexcept override { return typeid(T); }
        concept_base* clone_into(char* buf) const override {
            if constexpr (sizeof(model<T>) <= BUF) { // 编译期按 held 类型选路径
                return new (buf) model<T>(data);     // 就地,无堆分配
            }
            return nullptr; // 太大,塞不下
        }
        concept_base* clone_heap() const override { return new model<T>(data); }
    };

    alignas(std::max_align_t) char buffer_[BUF];
    concept_base* ptr_ = nullptr; // 指向 buffer_ 内部 或 堆
    bool owns_heap_ = false;

    void reset() {
        if (!ptr_)
            return;
        if (owns_heap_)
            delete ptr_;
        else
            ptr_->~concept_base(); // placement new 出来的对象要显式析构
        ptr_ = nullptr;
        owns_heap_ = false;
    }

  public:
    SboAny() = default;
    ~SboAny() { reset(); }

    template <typename T, typename D = std::decay_t<T>,
              typename = std::enable_if_t<!std::is_same_v<D, SboAny>>>
    SboAny(T&& v) {
        if constexpr (sizeof(model<D>) <= BUF) {
            ptr_ = new (buffer_) model<D>(std::forward<T>(v)); // 就地
            owns_heap_ = false;
        } else {
            ptr_ = new model<D>(std::forward<T>(v)); // 堆上
            owns_heap_ = true;
        }
    }

    SboAny(const SboAny& o) {
        if (!o.ptr_)
            return;
        ptr_ = o.ptr_->clone_into(buffer_); // 先试塞自己的 buffer
        if (!ptr_) {
            ptr_ = o.ptr_->clone_heap();
            owns_heap_ = true;
        }
    }
    SboAny& operator=(const SboAny&) = delete;
    SboAny(SboAny&&) = delete;
    SboAny& operator=(SboAny&&) = delete;

    bool has_value() const noexcept { return ptr_ != nullptr; }
    bool on_heap() const noexcept { return owns_heap_; }
    const std::type_info& type() const noexcept { return ptr_ ? ptr_->type() : typeid(void); }
};

struct Big {
    char data[128];
};

int main() {
    std::cout << "BUF = " << SboAny::BUF << " bytes (sizeof(void*)*3 on 64-bit)" << std::endl;

    std::cout << "\n=== 存 int:model<int> 很小,就地存 ===" << std::endl;
    g_alloc_count = 0;
    {
        SboAny a = 42;
        std::cout << "  type==int? " << (a.type() == typeid(int)) << ", on_heap? " << a.on_heap()
                  << ", 堆分配次数 = " << g_alloc_count << " (期望 0)\n";
    }

    std::cout << "\n=== 存 Big:sizeof(model<Big>) > BUF,堆上存 ===" << std::endl;
    g_alloc_count = 0;
    {
        SboAny a = Big{};
        std::cout << "  type==Big? " << (a.type() == typeid(Big)) << ", on_heap? " << a.on_heap()
                  << ", 堆分配次数 = " << g_alloc_count << " (期望 1)\n";
    }

    std::cout << "\n=== 拷贝 int:小类型拷贝走 clone_into,也不分配 ===" << std::endl;
    g_alloc_count = 0;
    {
        SboAny a = 7;
        SboAny b = a;
        std::cout << "  b.on_heap? " << b.on_heap() << ", 拷贝堆分配次数 = " << g_alloc_count
                  << " (期望 0)\n";
    }

    std::cout << "\n=== 拷贝 Big:大类型 clone_into 返回 nullptr,落回 clone_heap ===" << std::endl;
    {
        SboAny a = Big{};
        g_alloc_count = 0; // 只数拷贝这一次
        SboAny b = a;
        std::cout << "  b.on_heap? " << b.on_heap() << ", 拷贝堆分配次数 = " << g_alloc_count
                  << " (期望 1)\n";
    }
}
