#include <algorithm>
#include <vector>

std::vector<int> remove_all(std::vector<int> v, int dead) {
    v.erase(std::remove(v.begin(), v.end(), dead), v.end());
    return v;
}
