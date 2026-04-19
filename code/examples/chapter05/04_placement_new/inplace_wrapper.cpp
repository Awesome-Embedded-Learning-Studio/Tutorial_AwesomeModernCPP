#include <iostream>
#include <cstdio>
#include <string>
#include <new>
#include <type_traits>
#include <utility>
#include <cstdint>

// InPlace RAII Wrapper - 简化placement new的使用

template<typename T>
class InPlace {
    alignas(T) unsigned char storage_[sizeof(T)];
    bool constructed_ = false;

public:
    InPlace() noexcept = default;

    // 析构时自动清理
    ~InPlace() {
        destroy();
    }

    // 禁止拷贝
    InPlace(const InPlace&) = delete;
    InPlace& operator=(const InPlace&) = delete;

    // 支持移动
    InPlace(InPlace&& other) noexcept {
        if (other.constructed_) {
            new (storage_) T(std::move(*reinterpret_cast<T*>(other.storage_)));
            constructed_ = true;
            other.destroy();
        }
    }

    InPlace& operator=(InPlace&& other) noexcept {
        if (this != &other) {
            destroy();
            if (other.constructed_) {
                new (storage_) T(std::move(*reinterpret_cast<T*>(other.storage_)));
                constructed_ = true;
                other.destroy();
            }
        }
        return *this;
    }

    // 构造对象
    template<typename... Args>
    void construct(Args&&... args) {
        if (constructed_) {
            destroy();
        }
        new (storage_) T(std::forward<Args>(args)...);
        constructed_ = true;
    }

    // 显式析构
    void destroy() {
        if (constructed_) {
            reinterpret_cast<T*>(storage_)->~T();
            constructed_ = false;
        }
    }

    // 访问对象
    T* get() {
        return constructed_ ? reinterpret_cast<T*>(storage_) : nullptr;
    }

    const T* get() const {
        return constructed_ ? reinterpret_cast<const T*>(storage_) : nullptr;
    }

    T& operator*() {
        return *get();
    }

    const T& operator*() const {
        return *get();
    }

    T* operator->() {
        return get();
    }

    const T* operator->() const {
        return get();
    }

    explicit operator bool() const {
        return constructed_;
    }

    bool has_value() const {
        return constructed_;
    }
};

// 使用示例

struct Widget {
    int id;
    char name[32];

    Widget(int i, const char* n) : id(i) {
        std::snprintf(name, sizeof(name), "%s", n);
        std::cout << "Widget " << id << " constructed\n";
    }

    ~Widget() {
        std::cout << "Widget " << id << " destructed\n";
    }

    void print() const {
        std::cout << "Widget{id=" << id << ", name=" << name << "}\n";
    }

    void update(int new_id) {
        id = new_id;
    }
};

void basic_inplace_demo() {
    std::cout << "=== Basic InPlace Demo ===\n\n";

    InPlace<Widget> w;

    std::cout << "Before construct: has_value = " << w.has_value() << "\n";

    w.construct(1, "First Widget");
    std::cout << "After construct: has_value = " << w.has_value() << "\n";
    w->print();

    w->update(10);
    w->print();

    std::cout << "\nLeaving scope (auto destruct)...\n";
}

void reconstruct_demo() {
    std::cout << "\n=== Reconstruct Demo ===\n\n";

    InPlace<Widget> w;

    w.construct(1, "Version 1");
    w->print();

    std::cout << "\nReconstructing...\n";
    w.construct(2, "Version 2");
    w->print();

    std::cout << "\nReconstructing again...\n";
    w.construct(3, "Version 3");
    w->print();
}

void conditional_init_demo() {
    std::cout << "\n=== Conditional Initialization Demo ===\n\n";

    InPlace<int> value;

    bool use_default = true;

    if (use_default) {
        value.construct(42);
    } else {
        value.construct(100);
    }

    if (value) {
        std::cout << "Value = " << *value << "\n";
    }
}

// 可选值语义的InPlace（简化版std::optional）
template<typename T>
class Optional {
    alignas(T) unsigned char storage_[sizeof(T)];
    bool has_value_ = false;

public:
    Optional() noexcept = default;

    ~Optional() {
        reset();
    }

    Optional(const Optional& other) {
        if (other.has_value_) {
            new (storage_) T(*reinterpret_cast<const T*>(other.storage_));
            has_value_ = true;
        }
    }

    Optional(Optional&& other) noexcept {
        if (other.has_value_) {
            new (storage_) T(std::move(*reinterpret_cast<T*>(other.storage_)));
            has_value_ = true;
            other.reset();
        }
    }

    template<typename U>
    Optional(U&& value) {
        new (storage_) T(std::forward<U>(value));
        has_value_ = true;
    }

    Optional& operator=(const Optional& other) {
        if (this != &other) {
            reset();
            if (other.has_value_) {
                new (storage_) T(*reinterpret_cast<const T*>(other.storage_));
                has_value_ = true;
            }
        }
        return *this;
    }

    Optional& operator=(Optional&& other) noexcept {
        if (this != &other) {
            reset();
            if (other.has_value_) {
                new (storage_) T(std::move(*reinterpret_cast<T*>(other.storage_)));
                has_value_ = true;
                other.reset();
            }
        }
        return *this;
    }

    void reset() {
        if (has_value_) {
            reinterpret_cast<T*>(storage_)->~T();
            has_value_ = false;
        }
    }

    template<typename U>
    void emplace(U&& args) {
        reset();
        new (storage_) T(std::forward<U>(args));
        has_value_ = true;
    }

    T& operator*() { return *reinterpret_cast<T*>(storage_); }
    const T& operator*() const { return *reinterpret_cast<const T*>(storage_); }

    T* operator->() { return reinterpret_cast<T*>(storage_); }
    const T* operator->() const { return reinterpret_cast<const T*>(storage_); }

    explicit operator bool() const { return has_value_; }
    bool has_value() const { return has_value_; }
};

void optional_demo() {
    std::cout << "\n=== Optional (Custom Implementation) Demo ===\n\n";

    Optional<int> opt1;
    std::cout << "opt1 has value: " << opt1.has_value() << "\n";

    Optional<int> opt2(42);
    std::cout << "opt2 has value: " << opt2.has_value() << "\n";
    std::cout << "opt2 value: " << *opt2 << "\n";

    opt1.emplace(100);
    std::cout << "After emplace, opt1 value: " << *opt1 << "\n";

    opt1.reset();
    std::cout << "After reset, opt1 has value: " << opt1.has_value() << "\n";

    // 移动语义
    Optional<std::string> opt3(std::string("Hello"));
    std::cout << "opt3 value: " << *opt3 << "\n";

    Optional<std::string> opt4 = std::move(opt3);
    std::cout << "After move, opt3 has value: " << opt3.has_value() << "\n";
    std::cout << "opt4 value: " << *opt4 << "\n";
}

// 使用InPlace实现延迟初始化
class LazyWidget {
    InPlace<Widget> widget_;
    bool initialized_ = false;

public:
    void init(int id, const char* name) {
        if (!initialized_) {
            widget_.construct(id, name);
            initialized_ = true;
        }
    }

    Widget* get() {
        return initialized_ ? widget_.get() : nullptr;
    }

    bool is_initialized() const {
        return initialized_;
    }

    void print() const {
        if (initialized_) {
            widget_->print();
        } else {
            std::cout << "Widget not initialized\n";
        }
    }
};

void lazy_init_demo() {
    std::cout << "\n=== Lazy Initialization Demo ===\n\n";

    LazyWidget lw;

    std::cout << "Before init: " << lw.is_initialized() << "\n";
    lw.print();

    lw.init(99, "Lazy Widget");
    std::cout << "After init: " << lw.is_initialized() << "\n";
    lw.print();
}

int main() {
    basic_inplace_demo();
    reconstruct_demo();
    conditional_init_demo();
    optional_demo();
    lazy_init_demo();

    std::cout << "\n=== Key Takeaways ===\n";
    std::cout << "1. InPlace wrapper automates placement new and destructor\n";
    std::cout << "2. RAII ensures cleanup even on exceptions\n";
    std::cout << "3. Supports reconstruction and move semantics\n";
    std::cout << "4. Can implement optional/deferred initialization patterns\n";
    std::cout << "5. Zero heap allocation, all memory on stack\n";

    return 0;
}
