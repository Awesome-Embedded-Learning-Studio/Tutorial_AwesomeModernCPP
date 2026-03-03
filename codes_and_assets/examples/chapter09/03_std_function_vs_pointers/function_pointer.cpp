// Function Pointer Basics
// Demonstrates function pointer usage in embedded C++

#include <iostream>
#include <cstdint>

// Basic functions
int add(int a, int b) {
    return a + b;
}

int multiply(int a, int b) {
    return a * b;
}

// Function pointer declaration styles
void demo_declaration_styles() {
    std::cout << "=== Declaration Styles ===" << std::endl;

    // C-style typedef
    typedef int (*BinaryOpOld)(int, int);
    BinaryOpOld old_style = add;

    // C++11 using
    using BinaryOp = int(*)(int, int);
    BinaryOp new_style = multiply;

    // Direct variable
    int (*direct)(int, int) = add;

    std::cout << "old_style(3, 4) = " << old_style(3, 4) << std::endl;
    std::cout << "new_style(3, 4) = " << new_style(3, 4) << std::endl;
    std::cout << "direct(3, 4) = " << direct(3, 4) << std::endl;
}

// Array of function pointers (state machine style)
enum class State { IDLE, RUNNING, ERROR, SHUTDOWN };

void handle_idle() {
    std::cout << "State: IDLE - waiting for start" << std::endl;
}

void handle_running() {
    std::cout << "State: RUNNING - processing" << std::endl;
}

void handle_error() {
    std::cout << "State: ERROR - recovering" << std::endl;
}

void handle_shutdown() {
    std::cout << "State: SHUTDOWN - cleaning up" << std::endl;
}

void demo_state_table() {
    std::cout << "\n=== State Table ===" << std::endl;

    // State handler table
    struct StateHandler {
        State state;
        void (*handler)(void);
    };

    StateHandler state_table[] = {
        {State::IDLE, handle_idle},
        {State::RUNNING, handle_running},
        {State::ERROR, handle_error},
        {State::SHUTDOWN, handle_shutdown}
    };

    // Run state machine
    for (const auto& entry : state_table) {
        entry.handler();
    }
}

// ADC callback example
using ADCCallback = void(*)(int16_t value);

volatile uint32_t* const ADC_DR = reinterpret_cast<volatile uint32_t*>(0x4001204C);

ADCCallback g_adc_callback = nullptr;

void register_adc_callback(ADCCallback callback) {
    g_adc_callback = callback;
    std::cout << "ADC callback registered" << std::endl;
}

void my_adc_handler(int16_t value) {
    if (value > 4095) {
        std::cout << "ADC overflow: " << value << std::endl;
    } else {
        std::cout << "ADC value: " << value << std::endl;
    }
}

void demo_adc_callback() {
    std::cout << "\n=== ADC Callback ===" << std::endl;

    register_adc_callback(my_adc_handler);

    // Simulate ADC interrupt
    if (g_adc_callback) {
        g_adc_callback(2048);
    }
}

// Comparison function for sorting
int compare_asc(int a, int b) {
    return a - b;  // negative if a < b
}

int compare_desc(int a, int b) {
    return b - a;  // positive if a < b
}

void demo_sort_callback() {
    std::cout << "\n=== Sort Callback ===" << std::endl;

    int data[] = {5, 2, 8, 1, 9};

    // Using function pointer for custom sort
    int (*compare)(int, int) = compare_asc;
    std::cout << "Using compare_asc" << std::endl;

    // Simple bubble sort with callback
    for (size_t i = 0; i < 5; ++i) {
        for (size_t j = 0; j < 4 - i; ++j) {
            if (compare(data[j], data[j + 1]) > 0) {
                std::swap(data[j], data[j + 1]);
            }
        }
    }

    std::cout << "Sorted: ";
    for (int v : data) {
        std::cout << v << " ";
    }
    std::cout << std::endl;
}

// Function pointer as member
class Calculator {
public:
    using Operation = int(*)(int, int);

    int compute(int a, int b, Operation op) {
        if (op) {
            return op(a, b);
        }
        return 0;
    }
};

void demo_member_function_pointer() {
    std::cout << "\n=== Member with Function Pointer ===" << std::endl;

    Calculator calc;
    std::cout << "calc.compute(5, 3, add) = " << calc.compute(5, 3, add) << std::endl;
    std::cout << "calc.compute(5, 3, multiply) = " << calc.compute(5, 3, multiply) << std::endl;
}

// Size of function pointer
void demo_size() {
    std::cout << "\n=== Size Comparison ===" << std::endl;
    std::cout << "sizeof(int(*)(int, int)) = " << sizeof(int(*)(int, int)) << std::endl;
    std::cout << "sizeof(void(*)(void)) = " << sizeof(void(*)(void)) << std::endl;
    std::cout << "Function pointer is just an address - very lightweight!" << std::endl;
}

int main() {
    std::cout << "=== Function Pointer Demo ===" << std::endl;

    demo_declaration_styles();
    demo_state_table();
    demo_adc_callback();
    demo_sort_callback();
    demo_member_function_pointer();
    demo_size();

    return 0;
}
