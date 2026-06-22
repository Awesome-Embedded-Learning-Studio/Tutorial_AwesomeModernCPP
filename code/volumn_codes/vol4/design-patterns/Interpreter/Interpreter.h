#pragma once

#include <cctype>
#include <charconv>
#include <memory>
#include <stdexcept>
#include <string>

// ============================================================
// 第二步:interpret 接口 —— 把语法零件「类化」
// ============================================================
// 解释器模式的核心动作只有一个:把语法里的每一种构成元素对应到
// 程序里的一个类,每个类实现一个统一的「解释」方法。这里先建立
// 两条规矩:一个 Context(承载解释过程中需要被读写的全局状态),
// 一个统一的 interpret 接口(所有语法零件对外只暴露这一个入口)。

// Context:一开始很薄,只包住输入字符串;随着语言变复杂,它会越来越
// 像一个「解释器的运行时环境」(输入流、当前位置、变量表、错误信息)。
struct Context {
    std::string input;
    explicit Context(std::string s) : input(std::move(s)) {}
};

// Expression:整个模式的命门 —— 所有语法零件,不管多复杂,对外都
// 只暴露同一个接口。调用方拿到的永远是一个 Expression&,不需要
// 知道手里这棵子树到底是数字还是二元运算。
struct Expression {
    virtual ~Expression() = default;
    virtual long long interpret(const Context& ctx) const = 0;
};

// Number:这门小语言里最小的零件(文法里叫终结符 terminal)。
// 用 std::from_chars 真正去解析:从 pos 开始一直解析到字符串末尾,
// 遇到第一个非数字字符就停。last 是 past-the-end 迭代器,不是长度。
struct Number : Expression {
    std::size_t pos{0};
    explicit Number(std::size_t p) : pos(p) {}

    long long interpret(const Context& ctx) const override {
        const char* first = ctx.input.data() + pos;
        const char* last = ctx.input.data() + ctx.input.size();
        long long val = 0;
        auto [ptr, ec] = std::from_chars(first, last, val);
        if (ec != std::errc{}) {
            throw std::runtime_error("invalid number");
        }
        return val;
    }
};

// ============================================================
// 第三步:引入 AST —— 从「解释」到「求值」
// ============================================================
// 一旦输入有了运算符、优先级、括号、嵌套,单次字符串扫描就不够了:
// 先把文本解析成一棵抽象语法树(AST),再对这棵树递归求值。树一旦
// 建好,每个节点自己手里就攥着它需要的全部信息,所以求值接口不传
// Context,而是 evaluate() 直接返回值 —— 它和 interpret(Context&)
// 本质同源,都是一个「解释自己这个语法片段」的动作。

// Node:AST 的抽象基类,核心是一个纯虚 evaluate。
struct Node {
    virtual ~Node() = default;
    virtual long long evaluate() const = 0;
};

// NumberNode:AST 的叶子(终结符),直接攥着一个数值。
struct NumberNode : Node {
    long long value;
    explicit NumberNode(long long v) : value(v) {}
    long long evaluate() const override { return value; }
};

// BinaryNode:AST 的非叶子(二元运算,负责 + - * /)。用两个
// std::unique_ptr<Node> 持有左右子树 —— unique_ptr 的所有权语义
// 正好对应 AST 的树形结构:父节点独占子节点,整棵树销毁时递归
// 析构会自动把所有子节点连同释放掉,不用手写一行释放代码。
struct BinaryNode : Node {
    char op;
    std::unique_ptr<Node> left, right;
    BinaryNode(char o, std::unique_ptr<Node> l, std::unique_ptr<Node> r)
        : op(o), left(std::move(l)), right(std::move(r)) {}

    long long evaluate() const override {
        long long a = left->evaluate();
        long long b = right->evaluate();
        switch (op) {
            case '+':
                return a + b;
            case '-':
                return a - b;
            case '*':
                return a * b;
            case '/':
                if (b == 0)
                    throw std::runtime_error("division by zero");
                return a / b;
        }
        throw std::runtime_error("unknown operator");
    }
};

// ============================================================
// 第四步:把文本变成树 —— 递归下降解析器
// ============================================================
// 文法(近似 BNF),三层正好对应运算符优先级:
//   expression := term   (('+' | '-') term)*
//   term       := factor (('*' | '/') factor)*
//   factor     := number | '(' expression ')'
//   number     := ['-'? ] digit+
// 优先级靠「谁调用谁」的层次关系自然表达:加减在最外层,会去调
// term,而 term 会去调 factor,所以解析加号之前乘除早被更里层抓走了。
// 左结合靠 parse_term/parse_expression 里的 while 循环(而不是朴素
// 右递归)保证:1-2-3 解析成 ((1-2)-3)=-4,而不是 (1-(2-3))=2。
class Parser {
  public:
    explicit Parser(std::string s) : input_(std::move(s)), pos_(0) {}

    std::unique_ptr<Node> parse() {
        auto node = parse_expression();
        skip_spaces();
        if (pos_ != input_.size()) {
            throw std::runtime_error("unexpected input");
        }
        return node;
    }

  private:
    std::string input_;
    std::size_t pos_;

    void skip_spaces() {
        while (pos_ < input_.size() && std::isspace(static_cast<unsigned char>(input_[pos_]))) {
            ++pos_;
        }
    }

    std::unique_ptr<Node> parse_number() {
        skip_spaces();
        bool neg = false;
        if (pos_ < input_.size() && input_[pos_] == '-') {
            neg = true;
            ++pos_;
        }
        if (pos_ >= input_.size() || !std::isdigit(static_cast<unsigned char>(input_[pos_]))) {
            throw std::runtime_error("expected number");
        }
        long long val = 0;
        while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_]))) {
            val = val * 10 + (input_[pos_] - '0');
            ++pos_;
        }
        return std::make_unique<NumberNode>(neg ? -val : val);
    }

    std::unique_ptr<Node> parse_factor() {
        skip_spaces();
        if (pos_ < input_.size() && input_[pos_] == '(') {
            ++pos_; // consume '('
            auto node = parse_expression();
            skip_spaces();
            if (pos_ >= input_.size() || input_[pos_] != ')') {
                throw std::runtime_error("missing )");
            }
            ++pos_; // consume ')'
            return node;
        }
        return parse_number();
    }

    std::unique_ptr<Node> parse_term() {
        auto node = parse_factor();
        while (true) {
            skip_spaces();
            if (pos_ < input_.size() && (input_[pos_] == '*' || input_[pos_] == '/')) {
                char op = input_[pos_++];
                auto rhs = parse_factor();
                node = std::make_unique<BinaryNode>(op, std::move(node), std::move(rhs));
            } else {
                break;
            }
        }
        return node;
    }

    std::unique_ptr<Node> parse_expression() {
        auto node = parse_term();
        while (true) {
            skip_spaces();
            if (pos_ < input_.size() && (input_[pos_] == '+' || input_[pos_] == '-')) {
                char op = input_[pos_++];
                auto rhs = parse_term();
                node = std::make_unique<BinaryNode>(op, std::move(node), std::move(rhs));
            } else {
                break;
            }
        }
        return node;
    }
};
