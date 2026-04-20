#include <iostream>
#include <string>
#include <unordered_map>

int main()
{
    std::unordered_map<std::string, int> freq;
    freq["hello"] = 3;
    freq["world"] = 5;
    freq.emplace("cpp", 1);

    // 接口和 map 完全一致
    if (auto it = freq.find("hello"); it != freq.end()) {
        std::cout << it->first << ": " << it->second << "\n";
    }

    // 但遍历顺序不保证
    for (const auto& [word, count] : freq) {
        std::cout << word << " -> " << count << "\n";
    }

    return 0;
}
