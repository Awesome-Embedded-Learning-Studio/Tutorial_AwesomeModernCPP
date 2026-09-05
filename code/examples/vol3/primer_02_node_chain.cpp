// primer_02_node_chain.cpp
// 手工串一条链：三个节点 + 头插一个 99 + 遍历打印 + 逐个释放

#include <cstdio>

struct Node {
    int val;
    Node* next;
};

void push_front(Node*& head, int x) {
    Node* n = new Node{x, head}; // 新节点的 next 指向原来的头节点
    head = n;                    // head 改指新节点
}

int main() {
    Node* c = new Node{30, nullptr}; // 链尾：next 存 nullptr
    Node* b = new Node{20, c};       // 中间节点：next 指向 c
    Node* a = new Node{10, b};       // 头节点：next 指向 b
    Node* head = a;                  // 入口指向头节点

    push_front(head, 99);

    for (Node* cur = head; cur != nullptr; cur = cur->next) {
        std::printf("%d -> ", cur->val);
    }
    std::printf("nullptr\n");

    Node* cur = head; // 逐个释放节点，防止泄漏
    while (cur != nullptr) {
        Node* nxt = cur->next;
        delete cur;
        cur = nxt;
    }
    return 0;
}
