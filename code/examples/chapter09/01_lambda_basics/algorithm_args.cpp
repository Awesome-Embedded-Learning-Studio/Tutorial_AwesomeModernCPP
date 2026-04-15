// Lambda with STL Algorithms
// Demonstrates lambda expressions as arguments to STL algorithms

#include <iostream>
#include <vector>
#include <algorithm>
#include <numeric>

int main() {
    std::cout << "=== Lambda with STL Algorithms Demo ===" << std::endl;

    std::vector<int> readings = {12, 45, 23, 67, 34, 89, 56};

    // find_if: find first element matching condition
    auto it = std::find_if(readings.begin(), readings.end(),
                          [](int value) { return value > 50; });
    if (it != readings.end()) {
        std::cout << "First value > 50: " << *it << std::endl;
    }

    // count_if: count elements matching condition
    int count = std::count_if(readings.begin(), readings.end(),
                             [](int value) { return value > 80; });
    std::cout << "Values > 80: " << count << std::endl;

    // transform: apply function to each element
    std::vector<int> result;
    std::transform(readings.begin(), readings.end(), std::back_inserter(result),
                  [](int value) { return value * 2; });
    std::cout << "Transformed (x2): ";
    for (int v : result) {
        std::cout << v << " ";
    }
    std::cout << std::endl;

    // sort with custom comparator
    std::vector<int> data = {5, 2, 8, 1, 9};
    std::sort(data.begin(), data.end(), [](int a, int b) {
        return a > b;  // descending order
    });
    std::cout << "Sorted descending: ";
    for (int v : data) {
        std::cout << v << " ";
    }
    std::cout << std::endl;

    // accumulate with lambda
    int sum = std::accumulate(readings.begin(), readings.end(), 0,
                             [](int acc, int x) { return acc + x; });
    std::cout << "Sum: " << sum << std::endl;

    // for_each: perform action on each element
    std::cout << "Processing readings: ";
    std::for_each(readings.begin(), readings.end(), [](int value) {
        std::cout << "[" << value << "] ";
    });
    std::cout << std::endl;

    return 0;
}
