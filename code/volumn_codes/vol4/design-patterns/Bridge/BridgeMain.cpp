// 桥接模式 + pImpl 的演示入口。
// 第一部分:经典 Bridge —— 同一个 Circle,注入不同后端,行为不同。
// 第二部分:pImpl —— 验证 move 掏空源对象、深拷贝独立、ABI 压成一个指针。
#include "Bridge.h"
#include "Widget.h"

#include <iostream>
#include <memory>
#include <utility>
#include <vector>

// ---------- 第一部分:经典 Bridge(形状 x 后端) ----------
static void demo_classic_bridge() {
    std::cout << "== Bridge:形状 x 后端,两条链独立扩展 ===================\n";

    Circle a(1, 2, 3, std::make_unique<OpenGLApi>());
    Circle b(4, 5, 6, std::make_unique<DirectXApi>());

    std::cout << "a 用 OpenGL 后端:\n";
    a.draw();
    std::cout << "b 用 DirectX 后端:\n";
    b.draw();
    std::cout << "\n";
}

// ---------- 第二部分:pImpl ----------
static void demo_pimpl_move_and_copy() {
    std::cout << "== pImpl:move 掏空源对象,深拷贝独立 =====================\n";

    Widget a;
    for (int i = 0; i < 100; ++i) {
        a.push_back(42);
    }

    Widget b = a;            // 拷贝:深拷贝,独立
    Widget c = std::move(a); // move:源对象被掏空

    std::cout << "a.size after move = " << a.size() << " (expect 0,被 move 走了)\n";
    std::cout << "b.size = " << b.size() << ", b[0] = " << b.at(0)
              << " (expect 100, 42,深拷贝独立)\n";
    std::cout << "c.size = " << c.size() << " (expect 100,move 接管)\n";

    // 证明深拷贝真的独立:改 b 不影响 c
    b.push_back(7);
    std::cout << "after b.push_back(7): b.size = " << b.size() << ", c.size = " << c.size()
              << " (c 不受影响)\n";
    std::cout << "\n";
}

// 第二部分的小演示:do_work() 委托给 Impl
static void demo_pimpl_do_work() {
    std::cout << "== pImpl:do_work() 通过桥接委托给 Impl ===================\n";
    Widget w;
    for (int i = 0; i < 10; ++i) {
        w.push_back(i);
    }
    w.do_work();
    std::cout << "\n";
}

// 第二部分:ABI 验证 —— pImpl 之后 Widget 压成一个指针大小
static void demo_pimpl_abi() {
    std::cout << "== pImpl:ABI 稳定,Widget 压成一个指针 ===================\n";
    std::cout << "sizeof(Widget)  = " << sizeof(Widget) << "\n";
    std::cout << "sizeof(void*)   = " << sizeof(void*) << "\n";
    std::cout << "说明:Widget 压成一个指针大小,Impl 再怎么长,Widget 的 ABI 不变\n";
    std::cout << "\n";
}

int main() {
    demo_classic_bridge();
    demo_pimpl_move_and_copy();
    demo_pimpl_do_work();
    demo_pimpl_abi();
    return 0;
}
