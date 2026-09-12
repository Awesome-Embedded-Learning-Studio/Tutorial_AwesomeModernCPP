#include <iostream>
#include <string>
#include <string_view>

std::string make_greeting() {
    return "hello, modern cpp";
}

int main() {
    std::string_view view = make_greeting();
    std::cout << view << '\n';
}
