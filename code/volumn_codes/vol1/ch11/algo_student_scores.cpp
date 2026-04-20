#include <algorithm>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

struct Student {
    std::string name;
    double score;
};

void print_student(const Student& s)
{
    std::cout << "  " << s.name << ": " << s.score << "\n";
}

int main()
{
    std::vector<Student> students = {
        {"Alice",   92.5},
        {"Bob",     58.0},
        {"Charlie", 76.0},
        {"Diana",   88.5},
        {"Eve",     45.0},
        {"Frank",   95.0},
        {"Grace",   71.5},
    };

    // --- 1. 按成绩从高到低排序 ---
    std::sort(students.begin(), students.end(),
        [](const Student& a, const Student& b) { return a.score > b.score; });

    std::cout << "=== Ranking (high to low) ===\n";
    for (const auto& s : students) { print_student(s); }

    // --- 2. 查找最高分的学生 ---
    auto top = std::max_element(students.begin(), students.end(),
        [](const Student& a, const Student& b) { return a.score < b.score; });
    std::cout << "\nTop student: " << top->name
              << " (" << top->score << ")\n";

    // --- 3. 计算平均分 ---
    double sum = std::accumulate(students.begin(), students.end(), 0.0,
        [](double acc, const Student& s) { return acc + s.score; });
    std::cout << "Average score: "
              << sum / static_cast<double>(students.size()) << "\n";

    // --- 4. 统计及格和不及格的人数 ---
    int passing = std::count_if(students.begin(), students.end(),
        [](const Student& s) { return s.score >= 60.0; });
    std::cout << "Passing: " << passing
              << ", Failing: " << static_cast<int>(students.size()) - passing
              << "\n";

    // --- 5. 过滤不及格的学生（remove-erase）---
    std::vector<Student> filtered = students;
    auto it = std::remove_if(filtered.begin(), filtered.end(),
        [](const Student& s) { return s.score < 60.0; });
    filtered.erase(it, filtered.end());

    std::cout << "\n=== Passing students ===\n";
    for (const auto& s : filtered) { print_student(s); }

    return 0;
}
