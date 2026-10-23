#include <cstdio>
#include <cctype>
#include <cstdlib>
#include <string>

int main() {
    char line[64];
    while (std::fgets(line, sizeof line, stdin)) {
        std::string s(line);
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        bool digits = !s.empty();
        for (size_t i = 0; i < s.size(); ++i) {
            char ch = s[i];
            if (i == 0 && ch == '-' && s.size() > 1) continue;
            if (!std::isdigit((unsigned char)ch)) digits = false;
        }
        if (digits) std::printf("int %d\n", std::atoi(s.c_str()));
        else std::printf("text %s\n", s.c_str());
    }
}
