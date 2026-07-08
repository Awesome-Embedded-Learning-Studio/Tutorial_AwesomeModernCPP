// 侵入式引用计数:RefCountedThreadSafe + scoped_refptr
// 来源:WeakPtr 前置知识(一):侵入式引用计数与 scoped_refptr (pre-01)
// 编译:g++ -std=c++17 -Wall -Wextra 12_intrusive_refcount.cpp -o 12_intrusive_refcount -pthread

#include <atomic>
#include <cassert>
#include <iostream>
#include <memory>

namespace {

// 跨序列安全的侵入式引用计数基类(简化版 base::RefCountedThreadSafe)
class RefCountedThreadSafe {
  public:
    void add_ref() const noexcept { ref_count_.fetch_add(1, std::memory_order_relaxed); }
    bool release() const noexcept {
        // acq_rel:减法读到最新 count;归零时把"析构前的写"发布给接管 delete 的线程
        if (ref_count_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            return true; // 调用方负责 delete this
        }
        return false;
    }
    bool has_one_ref() const noexcept { return ref_count_.load(std::memory_order_acquire) == 1; }
    int use_count() const noexcept { return ref_count_.load(std::memory_order_acquire); }

  protected:
    RefCountedThreadSafe() = default;
    ~RefCountedThreadSafe() = default;

  private:
    mutable std::atomic<int> ref_count_{0};
};

// 侵入式智能指针外壳(简化版 base::scoped_refptr)
template <typename T> class scoped_refptr {
  public:
    scoped_refptr() noexcept = default;
    explicit scoped_refptr(T* p) noexcept : ptr_(p) {
        if (ptr_)
            ptr_->add_ref();
    }
    scoped_refptr(const scoped_refptr& o) noexcept : ptr_(o.ptr_) {
        if (ptr_)
            ptr_->add_ref();
    }
    scoped_refptr(scoped_refptr&& o) noexcept : ptr_(o.ptr_) { o.ptr_ = nullptr; }
    ~scoped_refptr() {
        if (ptr_ && ptr_->release())
            delete ptr_;
    }
    scoped_refptr& operator=(scoped_refptr r) noexcept {
        T* t = ptr_;
        ptr_ = r.ptr_;
        r.ptr_ = t; // copy-and-swap
        return *this;
    }
    T* get() const noexcept { return ptr_; }
    T& operator*() const noexcept { return *ptr_; }
    T* operator->() const noexcept { return ptr_; }
    explicit operator bool() const noexcept { return ptr_ != nullptr; }

  private:
    T* ptr_ = nullptr;
};

// 一个被引用计数的目标类型(析构 private,堵外部直接 delete)
class Flag : public RefCountedThreadSafe {
  public:
    Flag() = default;
    int id() const { return id_; }

  private:
    // 允许 scoped_refptr 在计数归零时 delete(Chromium 式受控析构)
    template <typename> friend class scoped_refptr;
    ~Flag() = default;
    int id_ = 42;
};

scoped_refptr<Flag> make_flag() {
    return scoped_refptr<Flag>(new Flag());
}

} // namespace

int main() {
    std::cout << "=== 侵入式引用计数 ===\n";

    auto p = make_flag(); // ref_count = 1
    std::cout << "  create:        use_count=" << p->use_count()
              << ", has_one_ref=" << p->has_one_ref() << "\n";

    {
        auto p2 = p; // copy → add_ref → ref_count = 2
        std::cout << "  copy p2:       use_count=" << p2->use_count()
                  << ", has_one_ref=" << p2->has_one_ref() << "\n";
    } // p2 析构 → release → ref_count = 1(不 delete)
    std::cout << "  p2 gone:       use_count=" << p->use_count() << "\n";

    {
        auto p3 = std::move(p); // move → 不增不减,p 变空
        std::cout << "  move to p3:    p valid=" << (p ? "yes" : "no")
                  << ", p3 use_count=" << p3->use_count() << "\n";
    } // p3 析构 → release → ref_count = 0 → delete

    std::cout << "\n=== 与 std::shared_ptr 的分配/大小对比 ===\n";
    // shared_ptr 非侵入式:控制块是独立堆分配(与对象分开);scoped_refptr 侵入式:计数器是对象成员
    struct Pod {
        int x;
    }; // 用一个平凡类型做 sizeof 对比(Flag 析构 private 不便塞 shared_ptr)
    std::cout << "  sizeof(scoped_refptr<Pod>)  = " << sizeof(scoped_refptr<Pod>)
              << " (1 个指针)\n";
    std::cout << "  sizeof(std::shared_ptr<Pod>) = " << sizeof(std::shared_ptr<Pod>)
              << " (2 个指针:对象 + 控制块)\n";
    std::cout << "  分配:shared_ptr<Pod>(new Pod) 2 次;make_shared 1 次但捆死内存;scoped_refptr 1 "
                 "次(计数嵌入对象)\n";
    return 0;
}
