#include <iostream>

int main() {
    int big = 30000;
    short small = big;
    short overflow = 40000;
    double pi = 3.14159;
    int int_pi = pi;
    std::cout << "overflow = " << overflow << "\n";
    std::cout << "int_pi = " << int_pi << "\n";
    return 0;
}
