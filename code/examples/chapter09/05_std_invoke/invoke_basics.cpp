// std::invoke Basics
// Demonstrates uniform invocation of different callable types

#include <iostream>
#include <functional>
#include <string>

// Free function
void free_function(int x) {
    std::cout << "free_function: " << x << std::endl;
}

// Functor
struct Functor {
    void operator()(int x) const {
        std::cout << "Functor: " << x << std::endl;
    }
};

// Class with member functions
struct Widget {
    void member_function(int x) {
        std::cout << "Widget::member_function: " << x << std::endl;
    }

    void const_member_function(int x) const {
        std::cout << "Widget::const_member_function: " << x << std::endl;
    }

    int data = 42;
};

void demo_invoke_basics() {
    std::cout << "=== std::invoke Basics ===" << std::endl;

    Functor f;
    Widget w;

    // Invoke free function
    std::invoke(free_function, 42);

    // Invoke functor
    std::invoke(f, 21);

    // Invoke lambda
    std::invoke([](int x) { std::cout << "Lambda: " << x << std::endl; }, 10);

    // Invoke member function with object reference
    std::invoke(&Widget::member_function, w, 5);

    // Invoke member function with object pointer
    std::invoke(&Widget::member_function, &w, 15);

    // Invoke const member function
    std::invoke(&Widget::const_member_function, w, 25);

    // Access member variable
    int value = std::invoke(&Widget::data, w);
    std::cout << "Widget::data = " << value << std::endl;
}

void demo_vs_traditional() {
    std::cout << "\n=== Traditional vs std::invoke ===" << std::endl;

    Widget w;

    // Traditional calling
    std::cout << "Traditional:" << std::endl;
    w.member_function(1);                  // Direct call
    (w.*&Widget::member_function)(2);      // Member pointer with ref
    (&w->*&Widget::member_function)(3);    // Member pointer with ptr

    // std::invoke - uniform syntax
    std::cout << "\nWith std::invoke:" << std::endl;
    std::invoke(&Widget::member_function, w, 1);
    std::invoke(&Widget::member_function, &w, 2);
}

void demo_with_std_function() {
    std::cout << "\n=== std::invoke with std::function ===" << std::endl;

    std::function<void(int)> func = [](int x) {
        std::cout << "std::function: " << x << std::endl;
    };

    std::invoke(func, 100);
}

void demo_reference_wrapper() {
    std::cout << "\n=== std::invoke with reference_wrapper ===" << std::endl;

    Widget w;
    auto ref = std::ref(w);

    std::invoke(&Widget::member_function, ref, 7);
}

int main() {
    demo_invoke_basics();
    demo_vs_traditional();
    demo_with_std_function();
    demo_reference_wrapper();

    std::cout << "\n=== Key Benefits ===" << std::endl;
    std::cout << "- Uniform syntax for all callable types" << std::endl;
    std::cout << "- No need to remember .* or ->* syntax" << std::endl;
    std::cout << "- Works with std::reference_wrapper" << std::endl;
    std::cout << "- Enables generic programming with callables" << std::endl;

    return 0;
}
