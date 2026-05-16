#include <concepts>
#include <type_traits>
#include <string>

template<typename T>
concept number = std::integral<T> || std::floating_point<T>;

static_assert(number<int>, "int should be number");
static_assert(number<double>, "double should be number");
static_assert(number<char>, "char is integral, so number");
static_assert(!number<std::string>, "string is not number");

int main() { return 0; }
