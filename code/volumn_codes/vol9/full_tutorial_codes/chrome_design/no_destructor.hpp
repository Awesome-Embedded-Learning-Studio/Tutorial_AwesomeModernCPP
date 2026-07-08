// no_destructor.hpp —— Chromium 风格 NoDestructor 的教学版完整实现
// 对应:NoDestructor 实战(二)~(四) + 设计指南(一)
// 设计要点(每个决策见 articles):
//   - alignas(T) char storage_[sizeof(T)]:对齐缓冲,零堆分配
//   - placement new 构造,完美转发
//   - ~NoDestructor()=default:只析构 char 成员,不调 ~T() —— 这是"不析构"的根
//   - static_assert 把关:只服务非平凡析构 T(平凡情况引导用 constinit/裸静态)
// 编译:g++/clang++ -std=c++20

#pragma once

#include <new>
#include <type_traits>
#include <utility>

namespace tamcpp::chrome {

template <typename T> class NoDestructor {
  public:
    // 通用:从任意参数完美转发给 T 的构造函数
    template <typename... Args> explicit NoDestructor(Args&&... args) {
        new (storage_) T(std::forward<Args>(args)...);
    }
    // 从 T 直接拷贝/移动构造(方便 initializer_list 等场景)
    explicit NoDestructor(const T& x) { new (storage_) T(x); }
    explicit NoDestructor(T&& x) { new (storage_) T(std::move(x)); }

    NoDestructor(const NoDestructor&) = delete;
    NoDestructor& operator=(const NoDestructor&) = delete;

    // 关键:=default 只析构 char 成员(平凡),不调 ~T()
    ~NoDestructor() = default;

    // 智能指针风格访问
    const T& operator*() const { return *get(); }
    T& operator*() { return *get(); }
    const T* operator->() const { return get(); }
    T* operator->() { return get(); }
    const T* get() const { return reinterpret_cast<const T*>(storage_); }
    T* get() { return reinterpret_cast<T*>(storage_); }

  private:
    // 把关:只服务非平凡析构 T;平凡情况引导用 constinit/裸静态
    static_assert(!(std::is_trivially_constructible_v<T> && std::is_trivially_destructible_v<T>),
                  "T is trivially constructible and destructible; "
                  "please use a constinit object of type T directly instead");
    static_assert(!std::is_trivially_destructible_v<T>,
                  "T is trivially destructible; "
                  "please use a function-local static of type T directly instead");

    alignas(T) char storage_[sizeof(T)];

    // Chromium 在 LEAK_SANITIZER 构建下额外持 T* storage_ptr_ 作 LSan reachability 根
    // (crbug/40562930);教学版省略,用 LSan suppression 文件替代。
};

} // namespace tamcpp::chrome
