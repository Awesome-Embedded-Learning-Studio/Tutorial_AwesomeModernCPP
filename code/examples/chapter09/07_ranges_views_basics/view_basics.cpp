// View Basics - Lazy Evaluation and Non-Owning Ranges
// Demonstrates fundamental view concepts in C++20 Ranges

#include <iostream>
#include <ranges>
#include <vector>
#include <array>

void demo_lazy_evaluation() {
    std::cout << "=== Lazy Evaluation Demo ===" << std::endl;

    std::vector<int> data = {1, 2, 3, 4, 5};

    // Create a view - nothing is computed yet
    auto filtered = std::views::filter(data, [](int x) {
        std::cout << "  Filtering: " << x << std::endl;
        return x > 2;
    });

    std::cout << "View created, now iterating..." << std::endl;
    for (int x : filtered) {
        std::cout << "Got: " << x << std::endl;
    }
}

void demo_non_owning() {
    std::cout << "\n=== Non-Owning View ===" << std::endl;

    std::vector<int> data = {10, 20, 30, 40, 50};

    auto view = std::views::filter(data, [](int x) { return x > 25; });

    std::cout << "First iteration: ";
    for (int x : view) {
        std::cout << x << " ";
    }
    std::cout << std::endl;

    // Modify original data
    data[2] = 5;
    data[3] = 100;

    std::cout << "After modification: ";
    for (int x : view) {
        std::cout << x << " ";
    }
    std::cout << std::endl;
}

void demo_o1_copy() {
    std::cout << "\n=== O(1) Copy ===" << std::endl;

    std::vector<int> data = {1, 2, 3, 4, 5};

    auto view1 = std::views::filter(data, [](int x) { return x > 2; });
    auto view2 = view1;  // O(1) copy - no element copying!

    std::cout << "view1 and view2 both refer to same underlying data" << std::endl;

    std::cout << "view1: ";
    for (int x : view1) {
        std::cout << x << " ";
    }
    std::cout << std::endl;

    std::cout << "view2: ";
    for (int x : view2) {
        std::cout << x << " ";
    }
    std::cout << std::endl;
}

void demo_range_interface() {
    std::cout << "\n=== Range Interface ===" << std::endl;

    std::vector<int> vec = {1, 2, 3, 4, 5};
    std::array<int, 4> arr = {10, 20, 30, 40};
    int native_arr[] = {100, 200, 300};

    // All are ranges
    auto view1 = std::views::filter(vec, [](int x) { return x > 2; });
    auto view2 = std::views::filter(arr, [](int x) { return x > 15; });
    auto view3 = std::views::filter(native_arr, [](int x) { return x > 150; });

    std::cout << "Vector view: ";
    for (int x : view1) std::cout << x << " ";
    std::cout << std::endl;

    std::cout << "Array view: ";
    for (int x : view2) std::cout << x << " ";
    std::cout << std::endl;

    std::cout << "Native array view: ";
    for (int x : view3) std::cout << x << " ";
    std::cout << std::endl;
}

void demo_view_lifetime() {
    std::cout << "\n=== View Lifetime Warning ===" << std::endl;

    // DON'T DO THIS
    /*
    auto get_bad_view() {
        std::vector<int> local = {1, 2, 3};
        return std::views::filter(local, [](int x) { return x > 1; });
        // local is destroyed, returned view is dangling!
    }
    */

    // DO THIS
    class DataView {
        std::vector<int> data_;
    public:
        DataView(std::initializer_list<int> init) : data_(init) {}

        auto get_view() {
            return std::views::filter(data_, [](int x) { return x > 1; });
            // data_ lives as long as the object
        }
    };

    DataView dv({1, 2, 3, 4, 5});
    auto safe_view = dv.get_view();

    std::cout << "Safe view: ";
    for (int x : safe_view) {
        std::cout << x << " ";
    }
    std::cout << std::endl;
}

void demo_const_view() {
    std::cout << "\n=== Const View ===" << std::endl;

    const std::vector<int> data = {1, 2, 3, 4, 5};

    auto view = std::views::transform(data, [](int x) {
        return x * 2;
    });

    std::cout << "Transformed const data: ";
    for (int x : view) {
        std::cout << x << " ";
    }
    std::cout << std::endl;

    // Original data unchanged
    std::cout << "Original data: ";
    for (int x : data) {
        std::cout << x << " ";
    }
    std::cout << std::endl;
}

int main() {
    demo_lazy_evaluation();
    demo_non_owning();
    demo_o1_copy();
    demo_range_interface();
    demo_view_lifetime();
    demo_const_view();

    std::cout << "\n=== Key Takeaways ===" << std::endl;
    std::cout << "- Views are lazy: computation happens on iteration" << std::endl;
    std::cout << "- Views don't own data: just a window into existing data" << std::endl;
    std::cout << "- Views are O(1) to copy: just a few pointers" << std::endl;
    std::cout << "- View lifetime must not exceed underlying data" << std::endl;

    return 0;
}
