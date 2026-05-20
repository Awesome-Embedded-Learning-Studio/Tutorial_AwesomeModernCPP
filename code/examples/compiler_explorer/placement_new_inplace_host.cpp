// placement_new_inplace_host.cpp
// placement new 的 RAII 封装：InPlace<T> 在无堆环境下安全构造与析构

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <new>
#include <utility>

template <typename T> class InPlace {
    alignas(T) unsigned char storage[sizeof(T)];
    bool constructed = false;

  public:
    InPlace() noexcept = default;

    template <typename... Args> void construct(Args&&... args) {
        if (constructed)
            destroy();
        new (storage) T(std::forward<Args>(args)...);
        constructed = true;
    }

    void destroy() {
        if (constructed) {
            reinterpret_cast<T*>(storage)->~T();
            constructed = false;
        }
    }

    T* get() { return constructed ? reinterpret_cast<T*>(storage) : nullptr; }
    T* operator->() { return get(); }
    ~InPlace() { destroy(); }
};

struct Sensor {
    int id;
    float value;
    Sensor(int i, float v) : id(i), value(v) {
        std::cout << "Sensor(" << id << ") constructed, value=" << value << "\n";
    }
    ~Sensor() { std::cout << "Sensor(" << id << ") destroyed\n"; }
    void read() { std::cout << "Reading sensor " << id << ": " << value << "\n"; }
};

int main() {
    InPlace<Sensor> slot;

    std::cout << "Before construct: has value? " << !!slot.get() << "\n";
    slot.construct(1, 36.5f);
    std::cout << "After construct: has value? " << !!slot.get() << "\n";
    slot->read();

    std::cout << "\nReconstructing with new args...\n";
    slot.construct(2, 42.0f);
    slot->read();

    std::cout << "\nLeaving scope (RAII cleanup)...\n";
    return 0;
}
