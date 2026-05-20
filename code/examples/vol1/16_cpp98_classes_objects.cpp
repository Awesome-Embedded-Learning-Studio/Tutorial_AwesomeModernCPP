// 16_cpp98_classes_objects.cpp
// 演示类定义、构造析构、this 指针、静态成员与 mutable

#include <cstdint>
#include <cstdio>

class StringBuilder {
  private:
    char buffer[256];
    size_t length;

  public:
    StringBuilder() : length(0) { buffer[0] = '\0'; }

    StringBuilder& append(const char* str) {
        while (*str && length < 255) {
            buffer[length++] = *str++;
        }
        buffer[length] = '\0';
        return *this;
    }

    const char* c_str() const { return buffer; }
};

class Sensor {
  private:
    int pin;
    mutable float cached_value;
    mutable bool cache_valid;
    mutable int read_count;
    static int total_instances;

  public:
    explicit Sensor(int p) : pin(p), cached_value(0), cache_valid(false), read_count(0) {
        ++total_instances;
    }

    ~Sensor() { --total_instances; }

    float read() const {
        ++read_count;
        if (!cache_valid) {
            cached_value = static_cast<float>(pin) * 0.5f + 20.0f;
            cache_valid = true;
        }
        return cached_value;
    }

    int get_read_count() const { return read_count; }

    static int get_total_instances() { return total_instances; }
};

int Sensor::total_instances = 0;

class UARTConfig {
  public:
    static const int DEFAULT_BAUDRATE = 115200;
    enum Parity { NONE, EVEN, ODD };

    static const char* parity_to_string(Parity p) {
        switch (p) {
            case NONE:
                return "NONE";
            case EVEN:
                return "EVEN";
            case ODD:
                return "ODD";
        }
        return "UNKNOWN";
    }
};

int main() {
    std::printf("=== StringBuilder (this pointer) ===\n");
    StringBuilder sb;
    sb.append("Hello").append(", ").append("World!");
    std::printf("%s\n", sb.c_str());

    std::printf("\n=== Sensor (static + mutable) ===\n");
    std::printf("Instances before: %d\n", Sensor::get_total_instances());
    {
        Sensor s1(5);
        Sensor s2(10);
        std::printf("Instances during: %d\n", Sensor::get_total_instances());

        float t1 = s1.read();
        float t2 = s1.read();
        std::printf("Readings: %.1f, %.1f\n", t1, t2);
        std::printf("s1 read count: %d\n", s1.get_read_count());

        const Sensor& ref = s2;
        float t3 = ref.read();
        std::printf("s2 via const ref: %.1f\n", t3);
    }
    std::printf("Instances after: %d\n", Sensor::get_total_instances());

    std::printf("\n=== Static Class Members ===\n");
    std::printf("Default baudrate: %d\n", UARTConfig::DEFAULT_BAUDRATE);
    std::printf("Parity: %s\n", UARTConfig::parity_to_string(UARTConfig::EVEN));

    return 0;
}
