#include <iostream>
#include <string>
#include <vector>

#include "greet.h"

int main() {
    std::cout << greet("WSL") << '\n';

    std::vector<int> nums{1, 2, 3, 4, 5};
    int sum = 0;
    for (int x : nums) {
        sum += x;
    }
    std::cout << "sum = " << sum << '\n';
    return 0;
}
