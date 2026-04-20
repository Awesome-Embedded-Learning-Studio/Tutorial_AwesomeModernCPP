#include <iostream>

int main()
{
    int numbers[4] = {100, 200, 300, 400};
    char chars[4]  = {'A', 'B', 'C', 'D'};

    int* pi = numbers;
    char* pc = chars;

    std::cout << "=== int* 步进 ===\n";
    std::cout << "pi:     " << pi << " -> *pi = " << *pi << "\n";
    std::cout << "pi + 1: " << (pi + 1) << " -> *(pi+1) = " << *(pi + 1) << "\n";
    std::cout << "pi + 2: " << (pi + 2) << " -> *(pi+2) = " << *(pi + 2) << "\n";

    std::cout << "\n=== char* 步进 ===\n";
    std::cout << "pc:     " << static_cast<void*>(pc)
              << " -> *pc = " << *pc << "\n";
    std::cout << "pc + 1: " << static_cast<void*>(pc + 1)
              << " -> *(pc+1) = " << *(pc + 1) << "\n";
    std::cout << "pc + 2: " << static_cast<void*>(pc + 2)
              << " -> *(pc+2) = " << *(pc + 2) << "\n";

    return 0;
}
