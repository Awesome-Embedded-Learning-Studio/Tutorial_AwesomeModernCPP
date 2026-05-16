#include <iostream>

int main() {
    int a = -1;
    unsigned int b = 2;
    std::cout << "(a < b) = " << (a < b) << "\n";  // prints 0 (false!) due to unsigned conversion
    return 0;
}
