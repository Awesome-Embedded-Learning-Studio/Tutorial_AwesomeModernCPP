// rvo_move.cpp — vol6 ch06-05 RVO/NRVO 与 move 的真实成本
// 用一个 copy/move 构造函数会计数的 Tracked 类型,直接数 RVO/NRVO/move/copy
// 各发生几次拷贝、几次移动。这不依赖计时(计时版被编译器 copy-elision 打穿),
// 是 RVO 教学最清楚的「编译器打不穿」演示。
// 编译: g++ -O2 -std=c++17 rvo_move.cpp -o rvo_move
//        对照 -fno-elide-constructors 看 RVO 关掉后变成什么(应为 1-2 次 move)
// 跑:   ./rvo_move   (无需 taskset,不计时)
#include <cstdint>
#include <cstdio>
#include <utility>

struct Tracked {
    int v;
    static int64_t copies, moves;
    Tracked(int x) : v(x) {}                         // 普通构造
    Tracked(const Tracked& o) : v(o.v) { ++copies; } // 拷贝构造
    Tracked(Tracked&& o) noexcept : v(o.v) {
        ++moves;
        o.v = -1;
    } // 移动构造
};
int64_t Tracked::copies = 0, Tracked::moves = 0;

// ① URVO:返回无名临时
Tracked make_urvo() {
    return Tracked(1);
}
// ② NRVO:返回有名局部
Tracked make_nrvo() {
    Tracked t(2);
    return t;
}
// ③ 强制 move:std::move 阻断 NRVO,强制走 move 构造
Tracked make_move() {
    Tracked t(3);
    return std::move(t);
}
// ④ 命名空间级 lvalue,不能 RVO/move,走拷贝
Tracked g_global(4);
Tracked make_copy() {
    return g_global;
}

void reset() {
    Tracked::copies = 0;
    Tracked::moves = 0;
}

int main() {
    std::printf("===== RVO/NRVO/move/copy 的拷贝/移动计数(编译器打不穿)=====\n");
    std::printf("(用 copy/move 构造函数计数的 Tracked 类型,直接看发生了几次)\n\n");

    reset();
    {
        auto x = make_urvo();
    }
    std::printf("  URVO return Tracked(1):    copies=%lld  moves=%lld  → 零拷贝零移动\n",
                (long long)Tracked::copies, (long long)Tracked::moves);

    reset();
    {
        auto x = make_nrvo();
    }
    std::printf("  NRVO return t(有名局部):  copies=%lld  moves=%lld  → 零拷贝零移动\n",
                (long long)Tracked::copies, (long long)Tracked::moves);

    reset();
    {
        auto x = make_move();
    }
    std::printf(
        "  return std::move(t):       copies=%lld  moves=%lld  → 被强制 1 次 move(NRVO 被你禁了)\n",
        (long long)Tracked::copies, (long long)Tracked::moves);

    reset();
    {
        auto x = make_copy();
    }
    std::printf("  return g_global(lvalue):   copies=%lld  moves=%lld  → 1 次拷贝(不能 RVO)\n",
                (long long)Tracked::copies, (long long)Tracked::moves);

    std::printf("\n结论:\n");
    std::printf("  - RVO/NRVO:零拷贝零移动(返回值直接在调用方栈上构造)。\n");
    std::printf("  - return std::move(局部):反而禁用 NRVO,多一次 move。所以【别】这么写。\n");
    std::printf("  - move 比 copy 便宜(指针交换 vs 深拷贝),对大对象差几个数量级。\n");
    std::printf("  - 加 -fno-elide-constructors 重编,URVO/NRVO 会退化成 1-2 次 move(看 RVO "
                "关掉的效果)。\n");
    return 0;
}
