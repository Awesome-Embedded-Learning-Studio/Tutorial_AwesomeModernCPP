#include "Widget.h"

#include <print>
#include <typeinfo>

// 用一个静态类型是基类、动态类型是派生类的对象,验证多态 clone() 保住了动态类型
// (不会切片),并且克隆体是一份独立副本(改它不影响原型,也不影响其他克隆体)。
static void demo_clone_preserves_type() {
    std::print("== demo: clone() preserves dynamic type ==\n");

    std::unique_ptr<Widget> proto = std::make_unique<Button>("OK");
    std::print("[proto]  dynamic type = {}\n", typeid(*proto).name());

    auto cloned = proto->clone(); // 虚派发 -> Button::clone
    std::print("[clone]  dynamic type = {}\n", typeid(*cloned).name());
    cloned->draw();
    std::print("\n");
}

// 验证注册表按名字克隆,且每次拿到的是独立副本。
static void demo_registry() {
    std::print("== demo: Prototype Registry ==\n");

    WidgetRegistry registry;

    // 注册原型:这些原型可以预先做昂贵的初始化
    registry.register_proto("ok_button", std::make_unique<Button>("OK"));
    registry.register_proto("cancel_button", std::make_unique<Button>("Cancel"));
    registry.register_proto("name_field", std::make_unique<TextField>("enter your name"));

    // 按名字克隆,改了克隆体不影响原型,也不影响其他克隆体
    auto b1 = registry.create("ok_button");
    auto b2 = registry.create("ok_button");
    auto f1 = registry.create("name_field");
    auto missing = registry.create("does_not_exist");

    if (b1)
        b1->draw(); // Button: OK
    if (b2)
        b2->draw(); // Button: OK (独立副本,改 b1 不影响 b2)
    if (f1)
        f1->draw(); // TextField: enter your name

    if (missing) {
        std::print("[missing] unexpected clone\n");
    } else {
        std::print("[missing] registry.create returned nullptr (not registered)\n");
    }
    std::print("\n");
}

int main() {
    demo_clone_preserves_type();
    demo_registry();
    return 0;
}
