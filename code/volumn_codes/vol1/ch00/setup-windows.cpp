#include <iostream>

int main()
{
    std::cout << "Hello from Windows C++ toolchain!" << std::endl;
    std::cout << "Compiler: "
#if defined(_MSC_VER)
              << "MSVC " << _MSC_VER
#elif defined(__GNUC__)
              << "GCC " << __GNUC__ << "." << __GNUC_MINOR__
#else
              << "Unknown"
#endif
              << std::endl;
    return 0;
}
