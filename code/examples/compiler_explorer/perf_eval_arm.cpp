// ARM freestanding: C vs C++ performance evaluation
// Uses only freestanding headers for ARM cross-compilation
#include <cstddef>
#include <cstdint>
#include <type_traits>

// ---- C-style GPIO ----
typedef struct {
    uint8_t pin;
    uint8_t state;
    uint8_t pwm_duty;
} CGPIO_Handle;

void c_gpio_init(CGPIO_Handle* h, uint8_t pin) {
    h->pin = pin;
    h->state = 0;
    h->pwm_duty = 0;
}

void c_gpio_toggle(CGPIO_Handle* h) {
    h->state = !h->state;
}

void example_c_gpio() {
    CGPIO_Handle led;
    c_gpio_init(&led, 5);
    c_gpio_toggle(&led);
    c_gpio_toggle(&led);
}

// ---- C++ OOP GPIO ----
class GpioDriver {
    uint8_t pin_;
    uint8_t state_;
    uint8_t pwm_duty_;

  public:
    explicit constexpr GpioDriver(uint8_t pin) : pin_(pin), state_(0), pwm_duty_(0) {}

    void toggle() { state_ = !state_; }
    uint8_t read() const { return state_; }
};

void example_cpp_gpio() {
    GpioDriver led(5);
    led.toggle();
    led.toggle();
}

// ---- C-style ring buffer ----
#define RB_SIZE 64

struct CRingBuf {
    uint8_t buf[RB_SIZE];
    uint16_t head;
    uint16_t tail;
    uint16_t count;
};

void rb_init(CRingBuf* rb) {
    rb->head = 0;
    rb->tail = 0;
    rb->count = 0;
}

bool rb_put(CRingBuf* rb, uint8_t d) {
    if (rb->count >= RB_SIZE)
        return false;
    rb->buf[rb->head] = d;
    rb->head = (rb->head + 1) % RB_SIZE;
    rb->count++;
    return true;
}

void example_c_rb() {
    CRingBuf rb;
    rb_init(&rb);
    rb_put(&rb, 'H');
    rb_put(&rb, 'i');
}

// ---- C++ template ring buffer ----
template <size_t N> class RingBuf {
    uint8_t buf_[N];
    uint16_t head_;
    uint16_t tail_;
    uint16_t count_;

  public:
    constexpr RingBuf() : buf_{}, head_(0), tail_(0), count_(0) {}

    bool put(uint8_t d) {
        if (count_ >= N)
            return false;
        buf_[head_] = d;
        head_ = (head_ + 1) % N;
        count_++;
        return true;
    }
};

void example_cpp_rb() {
    RingBuf<64> rb;
    rb.put('H');
    rb.put('i');
}

// ---- Size assertions ----
static_assert(sizeof(CGPIO_Handle) == 3);
static_assert(sizeof(GpioDriver) == 3);
static_assert(sizeof(CRingBuf) == RB_SIZE + 6);
