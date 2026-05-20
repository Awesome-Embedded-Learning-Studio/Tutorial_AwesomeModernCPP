// placement_new_inplace_arm.cpp
// ARM freestanding: InPlace<T> 零堆分配的对象构造

#include <cstddef>
#include <cstdint>

template <typename T> class InPlace {
    alignas(T) unsigned char storage[sizeof(T)];
    bool constructed;

  public:
    InPlace() noexcept : constructed(false) {}

    template <typename... Args> void construct(Args&&... args) {
        if (constructed)
            destroy();
        ::new (static_cast<void*>(storage)) T(static_cast<Args&&>(args)...);
        constructed = true;
    }

    void destroy() {
        if (constructed) {
            reinterpret_cast<T*>(storage)->~T();
            constructed = false;
        }
    }

    T* get() { return constructed ? reinterpret_cast<T*>(storage) : nullptr; }
    ~InPlace() { destroy(); }
};

struct Sensor {
    int id;
    float value;
};

extern "C" {
void demo_inplace() {
    InPlace<Sensor> slot;
    slot.construct(1, 36.5f);
    volatile Sensor* s = slot.get();
    (void)s;
    slot.destroy();
}
}
