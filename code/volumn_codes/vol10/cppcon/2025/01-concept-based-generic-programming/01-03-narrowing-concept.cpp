#include <concepts>
#include <type_traits>
#include <limits>

template<typename T>
concept number = std::integral<T> || std::floating_point<T>;

template<typename T, typename U>
concept smaller_range =
    number<T> && number<U> &&
    (std::numeric_limits<T>::max() < std::numeric_limits<U>::max() ||
     std::numeric_limits<T>::min() > std::numeric_limits<U>::min());

template<typename T, typename U>
concept narrowing_assign =
    number<T> && number<U> &&
    (
        smaller_range<T, U> ||
        (std::floating_point<U> && std::integral<T>) ||
        (std::integral<T> && std::integral<U> &&
         std::signed_integral<U> != std::signed_integral<T>)
    );

static_assert(narrowing_assign<short, int>, "int->short is narrowing");
static_assert(narrowing_assign<int, double>, "double->int is narrowing");
static_assert(narrowing_assign<unsigned int, int>, "int->unsigned may narrow");
static_assert(!narrowing_assign<int, short>, "short->int is not narrowing");
static_assert(!narrowing_assign<double, float>, "float->double is not narrowing");
static_assert(!narrowing_assign<int, int>, "int->int is not narrowing");

int main() { return 0; }
