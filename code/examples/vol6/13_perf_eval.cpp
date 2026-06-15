// Performance evaluation: C vs C++ code size comparison
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>

// ---- C-style GPIO driver ----
typedef struct {
    uint8_t pin;
    bool state;
    uint8_t pwm_duty;
} CGPIO_Handle;

void c_gpio_init(CGPIO_Handle* h, uint8_t pin) {
    h->pin = pin;
    h->state = false;
    h->pwm_duty = 0;
}
void c_gpio_write(CGPIO_Handle* h, bool val) {
    h->state = val;
}
void c_gpio_toggle(CGPIO_Handle* h) {
    c_gpio_write(h, !h->state);
}

// ---- C++ OOP GPIO driver ----
class GpioDriver {
    uint8_t pin_;
    bool state_;
    uint8_t pwm_duty_;

  public:
    explicit GpioDriver(uint8_t pin) : pin_(pin), state_(false), pwm_duty_(0) {}

    void write(bool val) { state_ = val; }
    void toggle() { write(!state_); }
    void set_pwm(uint8_t duty) { pwm_duty_ = (duty > 100) ? 100 : duty; }
    bool read() const { return state_; }
};

// ---- C-style ring buffer ----
#define RB_SIZE 64
struct CRingBuf {
    uint8_t buf[RB_SIZE];
    uint16_t head, tail, count;
};
void rb_init(CRingBuf* rb) {
    rb->head = rb->tail = rb->count = 0;
}
bool rb_put(CRingBuf* rb, uint8_t d) {
    if (rb->count >= RB_SIZE)
        return false;
    rb->buf[rb->head] = d;
    rb->head = (rb->head + 1) % RB_SIZE;
    rb->count++;
    return true;
}

// ---- C++ template ring buffer ----
template <size_t N = 64> class RingBuf {
    std::array<uint8_t, N> buf_{};
    uint16_t head_{0}, tail_{0}, count_{0};

  public:
    bool put(uint8_t d) {
        if (count_ >= N)
            return false;
        buf_[head_] = d;
        head_ = (head_ + 1) % N;
        count_++;
        return true;
    }
    bool get(uint8_t& d) {
        if (count_ == 0)
            return false;
        d = buf_[tail_];
        tail_ = (tail_ + 1) % N;
        count_--;
        return true;
    }
    uint16_t available() const { return count_; }
};

int main() {
    // GPIO comparison
    std::cout << "=== GPIO: C vs C++ ===\n";
    CGPIO_Handle c_led;
    c_gpio_init(&c_led, 5);
    c_gpio_write(&c_led, true);
    c_gpio_toggle(&c_led);
    std::cout << "C GPIO state: " << c_led.state << "\n";

    GpioDriver cpp_led(5);
    cpp_led.write(true);
    cpp_led.toggle();
    std::cout << "C++ GPIO state: " << cpp_led.read() << "\n";

    // Ring buffer comparison
    std::cout << "\n=== Ring Buffer: C vs C++ ===\n";
    CRingBuf c_rb;
    rb_init(&c_rb);
    const char* msg = "Hello";
    for (int i = 0; msg[i]; i++)
        rb_put(&c_rb, static_cast<uint8_t>(msg[i]));
    std::cout << "C ringbuf count: " << c_rb.count << "\n";

    RingBuf<64> cpp_rb;
    for (int i = 0; msg[i]; i++)
        cpp_rb.put(static_cast<uint8_t>(msg[i]));
    uint8_t byte;
    std::cout << "C++ ringbuf: ";
    while (cpp_rb.get(byte))
        std::cout << static_cast<char>(byte);
    std::cout << "\n";

    // Size comparison
    std::cout << "\n=== Sizeof ===\n";
    std::cout << "sizeof(CGPIO_Handle) = " << sizeof(CGPIO_Handle) << "\n";
    std::cout << "sizeof(GpioDriver)   = " << sizeof(GpioDriver) << "\n";
    std::cout << "sizeof(CRingBuf)     = " << sizeof(CRingBuf) << "\n";
    std::cout << "sizeof(RingBuf<64>)  = " << sizeof(RingBuf<64>) << "\n";

    return 0;
}
