// Standard: C++20
// 容器适配器：stack(LIFO) / queue(FIFO) / priority_queue(堆)——priority_queue 默认最大堆，greater
// 变最小堆
#include <functional>
#include <iostream>
#include <queue>
#include <stack>
#include <vector>

int main() {
    std::cout << "== stack：LIFO，top/push/pop 全在 back 一端 ==\n";
    std::stack<int> s;
    for (int x : {1, 2, 3}) {
        s.push(x);
    }
    std::cout << "push 1,2,3 后，top = " << s.top() << "（最后进的先出）\n";

    std::cout << "\n== queue：FIFO，push 在 back、front/pop 在 front ==\n";
    std::queue<int> q;
    for (int x : {1, 2, 3}) {
        q.push(x);
    }
    std::cout << "push 1,2,3 后，front = " << q.front() << " back = " << q.back() << '\n';

    std::cout << "\n== priority_queue 默认最大堆（vector + less）==\n";
    std::priority_queue<int> pq;
    for (int x : {5, 1, 9, 3, 7}) {
        pq.push(x);
    }
    std::cout << "依次 pop: ";
    while (!pq.empty()) {
        std::cout << pq.top() << ' ';
        pq.pop();
    }
    std::cout << '\n';

    std::cout << "\n== 换 greater 变最小堆 ==\n";
    std::priority_queue<int, std::vector<int>, std::greater<int>> min_pq;
    for (int x : {5, 1, 9, 3, 7}) {
        min_pq.push(x);
    }
    std::cout << "依次 pop: ";
    while (!min_pq.empty()) {
        std::cout << min_pq.top() << ' ';
        min_pq.pop();
    }
    std::cout << '\n';

    std::cout << "\n== 复杂度：top O(1)，push/pop O(log n) ==\n";
    std::cout << "（push = push_back + push_heap；pop = pop_heap + pop_back；底层就是堆算法）\n";
    return 0;
}
