// weak_ptr.hpp —— Chromium 风格 WeakPtr 的教学版完整实现
// 对应:WeakPtr 实战(二)~(五) + 设计指南(二)
// 设计要点(每个决策见 articles):
//   - Flag:RefCountedThreadSafe(原子引用计数)+ AtomicFlag(release/acquire liveness)
//   - WeakReference:scoped_refptr<const Flag> 的薄包装
//   - WeakPtr<T>:WeakReference + 允许悬垂的 T*;TRIVIAL_ABI;转换构造 requires convertible_to
//   - WeakPtrFactory<T>:铸币 + 批量失效(WeakReferenceOwner 析构即 invalidate)
//   - 序列检查:debug 下 lazy 绑定,release(NDEBUG)下 0 字节 no-op
// 编译:clang++ -std=c++20([[clang::trivial_abi]] 是 Clang 专有属性,GCC 全系列/MSVC 都不支持;
//        TAMCPP_TRIVIAL_ABI 宏在非 Clang 下展开为空,代码照常编译,仅无寄存器传递优化)

#pragma once

#include <atomic>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <thread>
#include <type_traits>
#include <utility>

#if __has_cpp_attribute(clang_trivial_abi) || \
    (defined(__clang__) && __has_cpp_attribute(clang::trivial_abi))
#    define TAMCPP_TRIVIAL_ABI [[clang::trivial_abi]]
#else
#    define TAMCPP_TRIVIAL_ABI
#endif

namespace tamcpp::chrome {

template <typename T> class WeakPtr;
template <typename T> class WeakPtrFactory;

namespace internal {

// ---- 跨序列安全的侵入式引用计数基类(简化 RefCountedThreadSafe)----
class RefCountedThreadSafe {
  public:
    void add_ref() const noexcept { ref_count_.fetch_add(1, std::memory_order_relaxed); }
    bool release() const noexcept {
        return ref_count_.fetch_sub(1, std::memory_order_acq_rel) == 1;
    }
    bool has_one_ref() const noexcept { return ref_count_.load(std::memory_order_acquire) == 1; }

  protected:
    RefCountedThreadSafe() = default;
    ~RefCountedThreadSafe() = default;

  private:
    mutable std::atomic<int> ref_count_{0};
};

// ---- 一次性 release/acquire 的原子标志(对应 base::AtomicFlag)----
class AtomicFlag {
  public:
    void Set() noexcept { flag_.store(1, std::memory_order_release); }
    bool IsSet() const noexcept { return flag_.load(std::memory_order_acquire) != 0; }

  private:
    std::atomic<uint_fast8_t> flag_{0};
};

// ---- 序列检查器:debug 下 lazy 绑定,release 下 0 字节 no-op ----
#if defined(NDEBUG)
class SequenceChecker {
  public:
    void detach_from_sequence() noexcept {}
    bool called_on_valid_sequence() const noexcept { return true; }
};
#else
class SequenceChecker {
  public:
    void detach_from_sequence() noexcept { bound_ = {}; }
    bool called_on_valid_sequence() const noexcept {
        if (!bound_) {
            bound_ = std::this_thread::get_id();
            return true;
        }
        return *bound_ == std::this_thread::get_id();
    }

  private:
    mutable std::optional<std::thread::id> bound_;
};
#endif

// ---- 侵入式智能指针外壳 ----
template <typename T> class scoped_refptr {
  public:
    scoped_refptr() noexcept = default;
    explicit scoped_refptr(T* p) noexcept : ptr_(p) {
        if (ptr_)
            ptr_->add_ref();
    }
    scoped_refptr(std::nullptr_t) noexcept {} // NOLINT(google-explicit-constructor)
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
        r.ptr_ = t;
        return *this;
    }
    scoped_refptr& operator=(std::nullptr_t) noexcept {
        if (ptr_ && ptr_->release())
            delete ptr_;
        ptr_ = nullptr;
        return *this;
    }
    T* get() const noexcept { return ptr_; }
    T& operator*() const noexcept { return *ptr_; }
    T* operator->() const noexcept { return ptr_; }
    explicit operator bool() const noexcept { return ptr_ != nullptr; }

  private:
    T* ptr_ = nullptr;
};

// ---- Flag:refcounted + 原子的 liveness ----
class Flag : public RefCountedThreadSafe {
  public:
    Flag() { seq_.detach_from_sequence(); } // 构造时未绑定(lazy)

    void Invalidate() noexcept {
        assert(seq_.called_on_valid_sequence() || has_one_ref()); // 跨线程析构豁免
        invalidated_.Set();                                       // release-store
    }
    bool IsValid() const noexcept {
        assert(seq_.called_on_valid_sequence()); // 首次触碰 → 绑定
        return !invalidated_.IsSet();            // acquire-load
    }
    bool MaybeValid() const noexcept {
        return !invalidated_.IsSet(); // 无序列断言,任意序列可调
    }

  private:
    template <typename> friend class scoped_refptr; // 允许计数归零时 delete
    ~Flag() = default;
    mutable SequenceChecker seq_;
    AtomicFlag invalidated_;
};

// ---- WeakReference:对 Flag 的引用包装 ----
class WeakReference {
  public:
    WeakReference() = default;
    explicit WeakReference(const scoped_refptr<Flag>& flag) : flag_(flag) {}
    bool IsValid() const noexcept { return flag_ && flag_->IsValid(); }
    bool MaybeValid() const noexcept { return flag_ && flag_->MaybeValid(); }
    void Reset() noexcept { flag_ = nullptr; }

  private:
    scoped_refptr<Flag> flag_;
};

// ---- WeakReferenceOwner:Flag 的发行方(析构即失效所有)----
class WeakReferenceOwner {
  public:
    WeakReferenceOwner() : flag_(new Flag()) {}
    ~WeakReferenceOwner() {
        if (flag_)
            flag_->Invalidate();
    }
    WeakReference GetRef() const { return WeakReference(flag_); }
    void Invalidate() {
        flag_->Invalidate();
        flag_ = scoped_refptr<Flag>(new Flag());
    }
    void InvalidateAndDoom() {
        flag_->Invalidate();
        flag_ = nullptr;
    }
    bool HasRefs() const { return !flag_->has_one_ref(); }

  private:
    scoped_refptr<Flag> flag_;
};

} // namespace internal

// ---- WeakPtr<T>:用户句柄 ----
template <typename T> class TAMCPP_TRIVIAL_ABI WeakPtr {
  public:
    WeakPtr() = default;
    WeakPtr(std::nullptr_t) noexcept {} // NOLINT(google-explicit-constructor)

    template <typename U>
        requires(std::convertible_to<U*, T*>)
    WeakPtr(const WeakPtr<U>& other) noexcept : ref_(other.ref_), ptr_(other.ptr_) {}
    template <typename U>
        requires(std::convertible_to<U*, T*>)
    WeakPtr(WeakPtr<U>&& other) noexcept : ref_(std::move(other.ref_)), ptr_(other.ptr_) {}

    T* get() const noexcept { return ref_.IsValid() ? ptr_ : nullptr; }
    T& operator*() const {
        assert(ref_.IsValid());
        return *ptr_;
    } // Chromium 用 CHECK
    T* operator->() const {
        assert(ref_.IsValid());
        return ptr_;
    }
    explicit operator bool() const noexcept { return get() != nullptr; }
    void reset() noexcept {
        ref_.Reset();
        ptr_ = nullptr;
    }

    bool maybe_valid() const noexcept { return ref_.MaybeValid(); }
    bool was_invalidated() const noexcept { return ptr_ && !ref_.IsValid(); }

  private:
    template <typename U> friend class WeakPtr;
    friend class WeakPtrFactory<T>;
    WeakPtr(internal::WeakReference&& ref, T* ptr) noexcept : ref_(std::move(ref)), ptr_(ptr) {
        assert(ptr);
    }

    internal::WeakReference ref_;
    T* ptr_ = nullptr; // RAW_PTR_EXCLUSION:允许悬垂,deref 前由 ref_ 守门
};

// ---- WeakPtrFactory<T>:铸币 + 批量失效 ----
template <typename T> class WeakPtrFactory {
  public:
    WeakPtrFactory() = delete;
    explicit WeakPtrFactory(T* ptr) : ptr_(reinterpret_cast<uintptr_t>(ptr)) { assert(ptr); }
    WeakPtrFactory(const WeakPtrFactory&) = delete;
    WeakPtrFactory& operator=(const WeakPtrFactory&) = delete;

    WeakPtr<const T> get_weak_ptr() const {
        return WeakPtr<const T>(owner_.GetRef(), reinterpret_cast<const T*>(ptr_));
    }
    WeakPtr<T> get_weak_ptr()
        requires(!std::is_const_v<T>)
    {
        return WeakPtr<T>(owner_.GetRef(), reinterpret_cast<T*>(ptr_));
    }
    WeakPtr<T> get_mutable_weak_ptr() const
        requires(!std::is_const_v<T>)
    {
        return WeakPtr<T>(owner_.GetRef(), reinterpret_cast<T*>(ptr_));
    }

    void invalidate_weak_ptrs() {
        assert(ptr_);
        owner_.Invalidate();
    }
    void invalidate_weak_ptrs_and_doom() {
        assert(ptr_);
        owner_.InvalidateAndDoom();
        ptr_ = 0;
    }
    bool has_weak_ptrs() const { return ptr_ && owner_.HasRefs(); }

  private:
    internal::WeakReferenceOwner owner_;
    uintptr_t ptr_;
};

} // namespace tamcpp::chrome
