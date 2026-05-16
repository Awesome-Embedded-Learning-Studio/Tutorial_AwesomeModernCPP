#include <cstdint>
#include <span>
#include <vector>
#include <iostream>

void process(std::span<const uint8_t> data) {
    std::cout << "got " << data.size() << " bytes\n";
}

int main() {
    std::vector<uint8_t> vec = {1, 2, 3, 4, 5};
    process(vec);
    uint8_t arr[] = {10, 20, 30};
    process(arr);
    return 0;
}
