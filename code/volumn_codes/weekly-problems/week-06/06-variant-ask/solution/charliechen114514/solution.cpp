#include <string>
#include <variant>

int fetch_int(const std::variant<int, std::string>& v) {
    if (const int* p = std::get_if<int>(&v)) {
        return *p;
    }
    return -1;
}
