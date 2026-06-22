#include "Composite.h"

#include <iostream>

// 演示第一条路:把同类对象聚合成数组。
// 文章蓝本:hero{10, 20, 30} → sum=60, avg=20, max=30。
void demo_array_aggregation() {
    std::cout << "===== Path 1: aggregate homogeneous objects into an array =====\n";

    Creature hero{10, 20, 30};

    // 用写死了字段清单的 CreatureAnalyzer。
    CreatureAnalyzer analyzer(hero);
    std::cout << "CreatureAnalyzer(hero):\n"
              << "  sum = " << analyzer.sum() << '\n'
              << "  avg = " << analyzer.avg() << '\n'
              << "  max = " << analyzer.max() << '\n';

    // 用通用 Analyzer<T, Getters...>:std::invoke 把一包 getter 展开成数组。
    // getters 传成员指针(&Creature::get_strength 等),由 std::invoke 统一调用。
    Analyzer an(hero, &Creature::get_strength, &Creature::get_agility, &Creature::get_intelligence);
    std::cout << "Analyzer<int>(hero, &get_strength, &get_agility, &get_intelligence):\n"
              << "  sum = " << an.sum() << '\n'
              << "  avg = " << an.avg() << '\n'
              << "  max = " << an.max() << '\n';

    std::cout << '\n';
}

// 演示第二条路:经典树形 Composite(安全式)。
// 文章蓝本:root 挂 Circle("A");root 挂子组 sub,sub 挂 Circle("B")、Circle("C");
//          root 再挂 Circle("D")。调用方只调一次 root.draw(),整棵子树被完整展开。
void demo_classic_tree() {
    std::cout << "===== Path 2: classic tree-shaped Composite (safe variant) =====\n";

    Group root("root");
    root.add(std::make_unique<Circle>("A"));

    auto sub = std::make_unique<Group>("sub");
    sub->add(std::make_unique<Circle>("B"));
    sub->add(std::make_unique<Circle>("C"));
    root.add(std::move(sub));

    root.add(std::make_unique<Circle>("D"));

    root.draw();
}

int main() {
    demo_array_aggregation();
    demo_classic_tree();
    return 0;
}
