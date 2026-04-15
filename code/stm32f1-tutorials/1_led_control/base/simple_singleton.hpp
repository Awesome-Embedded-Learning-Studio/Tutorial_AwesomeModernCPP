#pragma once

namespace base {
template <typename SingletonClass> class SimpleSingleton {
  public:
    SimpleSingleton() = default;
    ~SimpleSingleton() = default;

    static SingletonClass& instance() {
        static SingletonClass _instance;
        return _instance;
    }

  private:
    /* Never Shell A Single Instance Copyable And Movable */
    SimpleSingleton(const SimpleSingleton&) = delete;
    SimpleSingleton(SimpleSingleton&&) = delete;
    SimpleSingleton& operator=(const SimpleSingleton&) = delete;
    SimpleSingleton& operator=(SimpleSingleton&&) = delete;
};
} // namespace base
