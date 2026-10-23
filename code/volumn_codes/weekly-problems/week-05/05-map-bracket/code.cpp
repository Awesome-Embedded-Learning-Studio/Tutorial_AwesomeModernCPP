#include <cstdio>
#include <map>
#include <string>

int main() {
    std::map<std::string, int> stock{{"apple", 3}, {"pear", 0}};
    int known = 0;
    for (const char* name : {"apple", "pear", "peach"}) {
        if (stock[name] > 0) {
            ++known;
        }
    }
    std::printf("known=%d size=%d\n", known, (int)stock.size());
}
