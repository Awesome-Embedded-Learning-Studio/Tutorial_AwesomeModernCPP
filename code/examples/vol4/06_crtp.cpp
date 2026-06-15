// CRTP: Curiously Recurring Template Pattern demo
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <stdexcept>

// ---- Object Counter with CRTP ----
template <typename Derived> class ObjectCounter {
  public:
    static size_t get_count() { return count_; }

  protected:
    ObjectCounter() { ++count_; }
    ObjectCounter(const ObjectCounter&) { ++count_; }
    ObjectCounter(ObjectCounter&&) { ++count_; }
    ~ObjectCounter() { --count_; }

  private:
    static size_t count_;
};
template <typename Derived> size_t ObjectCounter<Derived>::count_ = 0;

// A sensor class that uses the counter
class Sensor : public ObjectCounter<Sensor> {
  public:
    explicit Sensor(int id) : id_(id) {
        printf("[Sensor] #%d created. Total: %zu\n", id_, get_count());
    }
    ~Sensor() { printf("[Sensor] #%d destroyed. Remaining: %zu\n", id_, get_count()); }

  private:
    int id_;
};

// ---- Singleton with CRTP ----
template <typename Derived> class Singleton {
  public:
    Singleton(const Singleton&) = delete;
    Singleton& operator=(const Singleton&) = delete;

    static Derived& instance() {
        static Derived inst;
        return inst;
    }

  protected:
    Singleton() = default;
};

class DeviceManager : public Singleton<DeviceManager> {
    friend class Singleton<DeviceManager>;

  public:
    bool register_device(const char* name, void* device) {
        if (count_ >= max_devices_)
            return false;
        devices_[count_] = {name, device};
        count_++;
        return true;
    }
    void* get_device(const char* name) const {
        for (size_t i = 0; i < count_; ++i) {
            if (strcmp(devices_[i].name, name) == 0)
                return devices_[i].device;
        }
        return nullptr;
    }
    size_t device_count() const { return count_; }

  private:
    struct DeviceEntry {
        const char* name;
        void* device;
    };
    static constexpr size_t max_devices_ = 16;
    DeviceEntry devices_[max_devices_];
    size_t count_ = 0;
    DeviceManager() = default;
};

int main() {
    std::cout << "=== Object Counter Demo ===\n";
    {
        Sensor s1(1);
        Sensor s2(2);
        {
            Sensor s3(3);
        }
        Sensor s4(4);
    }
    std::cout << "After all destroyed: " << Sensor::get_count() << "\n";

    std::cout << "\n=== Singleton Demo ===\n";
    auto& dm = DeviceManager::instance();
    int uart1 = 1, spi1 = 2;
    dm.register_device("UART1", &uart1);
    dm.register_device("SPI1", &spi1);

    void* dev = dm.get_device("UART1");
    std::cout << "Found UART1: " << (dev ? "yes" : "no") << "\n";
    std::cout << "Found I2C1:  " << (dm.get_device("I2C1") ? "yes" : "no") << "\n";
    std::cout << "Device count: " << dm.device_count() << "\n";

    return 0;
}
