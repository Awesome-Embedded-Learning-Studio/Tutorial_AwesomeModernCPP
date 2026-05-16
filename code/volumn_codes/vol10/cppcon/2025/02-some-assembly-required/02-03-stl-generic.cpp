#include <algorithm>
#include <iostream>
#include <string>

struct Person {
    std::string name;
    int age;
};
bool operator<(const Person& a, const Person& b) { return a.age < b.age; }

int main() {
    Person people[] = {{"Alice", 30}, {"Bob", 25}, {"Charlie", 35}};
    std::sort(std::begin(people), std::end(people));
    for (const auto& p : people) std::cout << p.name << ": " << p.age << '\n';
}
