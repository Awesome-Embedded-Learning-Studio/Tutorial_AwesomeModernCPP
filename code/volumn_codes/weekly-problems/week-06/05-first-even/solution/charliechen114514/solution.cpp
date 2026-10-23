#include <optional>
#include <vector>

std::optional<int> first_even(const std::vector<int>& v) {
    for (int x : v) {
        if (x % 2 == 0) {
            return x;
        }
    }
    return std::nullopt;
}
