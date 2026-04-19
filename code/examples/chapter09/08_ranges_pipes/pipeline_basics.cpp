// Pipeline Basics - The Pipe Operator
// Demonstrates composing range operations with the `|` operator

#include <iostream>
#include <ranges>
#include <vector>
#include <numeric>

void demo_basic_pipeline() {
    std::cout << "=== Basic Pipeline ===" << std::endl;

    std::vector<int> data = {1, 2, 3, 4, 5};

    // Traditional nested calls (hard to read)
    auto result1 = std::views::transform(
        std::views::filter(data, [](int x) { return x > 2; }),
        [](int x) { return x * 2; }
    );
    (void)result1;

    // Pipeline style (easy to read)
    auto result2 = data
        | std::views::filter([](int x) { return x > 2; })
        | std::views::transform([](int x) { return x * 2; });

    std::cout << "Pipeline result: ";
    for (int x : result2) {
        std::cout << x << " ";
    }
    std::cout << std::endl;
}

void demo_three_stage() {
    std::cout << "\n=== Three-Stage Pipeline ===" << std::endl;

    std::vector<int> readings = {12, 45, 23, 67, 34, 89, 56};

    // Filter -> Transform -> Take
    auto processed = readings
        | std::views::filter([](int v) { return v >= 50 && v <= 300; })
        | std::views::transform([](int v) { return v * 3.3f / 4095; })
        | std::views::take(5);

    std::cout << "Processed voltages: ";
    for (float v : processed) {
        std::cout << v << " V ";
    }
    std::cout << std::endl;
}

void demo_nested_vs_pipe() {
    std::cout << "\n=== Nested vs Pipe Comparison ===" << std::endl;

    std::vector<int> data = {1, 2, 3, 4, 5};

    // Nested (difficult to read)
    std::cout << "Nested: ";
    auto nested = std::views::take(
        std::views::transform(
            std::views::drop(data, 1),
            [](int x) { return x * x; }
        ),
        3
    );
    for (int x : nested) {
        std::cout << x << " ";
    }
    std::cout << std::endl;

    // Pipe (flows naturally)
    std::cout << "Pipe:    ";
    auto piped = data
        | std::views::drop(1)
        | std::views::transform([](int x) { return x * x; })
        | std::views::take(3);
    for (int x : piped) {
        std::cout << x << " ";
    }
    std::cout << std::endl;
}

void demo_complex_pipeline() {
    std::cout << "\n=== Complex Pipeline ===" << std::endl;

    std::vector<int> data = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

    // Multiple stages
    auto result = data
        | std::views::filter([](int x) { return x % 2 == 0; })  // evens
        | std::views::transform([](int x) { return x * x; })   // square
        | std::views::take(3);                                 // first 3

    std::cout << "Even numbers squared (first 3): ";
    for (int x : result) {
        std::cout << x << " ";
    }
    std::cout << std::endl;
}

void demo_accumulate_with_pipeline() {
    std::cout << "\n=== Accumulate Pipeline Results ===" << std::endl;

    std::vector<int> data = {1, 2, 3, 4, 5};

    // Sum of squared evens
    auto squared_evens = data
        | std::views::filter([](int x) { return x % 2 == 0; })
        | std::views::transform([](int x) { return x * x; });

    int sum = std::accumulate(squared_evens.begin(), squared_evens.end(), 0);
    std::cout << "Sum of squared evens: " << sum << std::endl;
}

void demo_to_vector() {
    std::cout << "\n=== Convert to Container ===" << std::endl;

    std::vector<int> data = {1, 2, 3, 4, 5};

    // Process and collect into vector
    auto processed = data
        | std::views::filter([](int x) { return x > 2; })
        | std::views::transform([](int x) { return x * 10; });

    std::vector<int> result(processed.begin(), processed.end());

    std::cout << "Collected: ";
    for (int x : result) {
        std::cout << x << " ";
    }
    std::cout << std::endl;
}

int main() {
    demo_basic_pipeline();
    demo_three_stage();
    demo_nested_vs_pipe();
    demo_complex_pipeline();
    demo_accumulate_with_pipeline();
    demo_to_vector();

    std::cout << "\n=== Key Points ===" << std::endl;
    std::cout << "- Pipe operator `|` creates readable left-to-right flows" << std::endl;
    std::cout << "- Each stage is lazy (only computes on iteration)" << std::endl;
    std::cout << "- No temporary allocations" << std::endl;
    std::cout << "- Easy to modify pipeline by adding/removing stages" << std::endl;

    return 0;
}
