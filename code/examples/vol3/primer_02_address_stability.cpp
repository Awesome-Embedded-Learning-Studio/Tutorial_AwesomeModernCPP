// primer_02_address_stability.cpp
// 链表从不搬家：删掉中间一个节点，其余节点的地址一位都不动

#include <cstdio>
#include <list>

int main() {
    std::list<int> L{0, 1, 2, 3, 4, 5, 6, 7};

    auto dump = [&L](const char* tag) {
        std::printf("%s:\n", tag);
        int i = 0;
        for (int& x : L) {
            std::printf("  [%d] addr=%p val=%d\n", i++, static_cast<void*>(&x), x);
        }
    };

    dump("before erase");

    for (auto it = L.begin(); it != L.end(); ++it) {
        if (*it == 4) {
            L.erase(it); // 只解除这一个节点，其余节点不动
            break;
        }
    }

    dump("after  erase (node holding 4 removed)");
    return 0;
}
