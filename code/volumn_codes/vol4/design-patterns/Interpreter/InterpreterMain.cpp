#include "Interpreter.h"

#include <iostream>
#include <string>
#include <vector>

// 第二步的演示:单数字走 interpret(Context&) 这条路。
// 即使语言暂时只有一个数字,Context + 统一 interpret 接口这两条
// 规矩已经立下,后面加加减乘除括号都是顺着它们走。
void demo_single_number() {
    std::cout << "Step 2: interpret(Context&) on a single number\n";
    Context ctx("12345");
    auto expr = std::make_unique<Number>(0);
    std::cout << "  \"12345\".interpret() = " << expr->interpret(ctx) << "\n";
    std::cout << '\n';
}

// 第四步的演示:把文本喂给 Parser 建出 AST,再对树递归求值。
// 覆盖优先级、括号、多层嵌套、纯数字,以及除零 / 缺右括号两条错误路径。
void demo_calculator() {
    std::cout << "Step 4: recursive-descent parser + AST evaluate\n";
    std::vector<std::string> tests = {
        "1+2*3",           // 期望 7:* 优先于 +
        "(1+2)*3",         // 期望 9:括号
        "10 - 4 / 2",      // 期望 8:/ 优先于 -
        "(2+3)*(4-1)",     // 期望 15
        "100",             // 期望 100:纯数字
        "2 * (3 + 4) * 5", // 期望 70:多层
        "1/0",             // 错误:除零
        "(1+2",            // 错误:缺右括号
    };
    for (const auto& t : tests) {
        try {
            Parser p(t);
            auto ast = p.parse();
            std::cout << "  \"" << t << "\" = " << ast->evaluate() << '\n';
        } catch (const std::exception& e) {
            std::cout << "  \"" << t << "\" -> ERROR: " << e.what() << '\n';
        }
    }
    std::cout << '\n';
}

int main() {
    demo_single_number();
    demo_calculator();
    return 0;
}
