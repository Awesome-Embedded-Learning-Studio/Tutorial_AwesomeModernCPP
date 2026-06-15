// Standard: C++20
// deque / list / forward_list：vector 之外的三个选择——头插都 O(1)，但内存布局与 splice 各异
#include <deque>
#include <forward_list>
#include <iostream>
#include <list>

int main() {
    std::cout << "== 三者头插都 O(1)，但存储布局不同 ==\n";
    constexpr int N = 100'000;

    std::deque<int> dq;
    for (int i = 0; i < N; ++i) {
        dq.push_front(i); // 分段连续，头尾均 O(1)
    }

    std::list<int> lt;
    for (int i = 0; i < N; ++i) {
        lt.push_front(i); // 双向链表节点
    }

    std::forward_list<int> fl;
    for (int i = 0; i < N; ++i) {
        fl.push_front(i); // 单向链表，最省内存（无 prev 指针）
    }
    std::cout << "各头插 " << N << " 个，复杂度都是 O(1)\n";

    std::cout << "\n== sizeof：forward_list 最省，deque 有分段控制开销 ==\n";
    std::cout << "sizeof(deque<int>)        = " << sizeof(dq) << '\n';
    std::cout << "sizeof(list<int>)         = " << sizeof(lt) << '\n';
    std::cout << "sizeof(forward_list<int>) = " << sizeof(fl) << '\n';

    std::cout << "\n== list::splice：O(1) 把整串节点搬过来，零拷贝 ==\n";
    std::list<int> a{1, 2, 3};
    std::list<int> b{10, 20};
    auto it = a.begin();
    ++it;            // 指向元素 2
    a.splice(it, b); // 把 b 整个接到 a 的 it 之前，搬节点不拷贝
    std::cout << "splice 后 a = ";
    for (auto x : a) {
        std::cout << x << ' ';
    }
    std::cout << "\nb 现在 size = " << b.size() << "（节点被搬走，b 空了）\n";

    std::cout << "\n== forward_list 没有 size()：单向链表数元素要 O(n) ==\n";
    int cnt = 0;
    for (auto x : fl) {
        (void)x;
        ++cnt;
    }
    std::cout << "forward_list 手动数 = " << cnt << '\n';
    return 0;
}
