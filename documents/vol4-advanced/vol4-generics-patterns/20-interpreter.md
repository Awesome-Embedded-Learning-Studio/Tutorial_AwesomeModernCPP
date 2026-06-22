---
title: "解释器模式:把一门小语言塞进你的程序里"
description: "从最直白的「直接 std::stoi」开始,一步步逼出 interpret 接口和 AST,用递归下降实现一个能跑 + - * / 括号的计算器,再讲清什么时候根本不该用这个模式"
chapter: 11
order: 20
tags:
  - host
  - cpp-modern
  - intermediate
  - 解释器模式
difficulty: intermediate
platform: host
cpp_standard: [11, 17, 20]
reading_time_minutes: 22
related:
  - "单例模式:从注释约束到 Meyer's Singleton"
prerequisites:
  - "Chapter 6: 类与对象"
  - "Chapter 9: 智能指针与所有权"
---

# 解释器模式:把一门小语言塞进你的程序里

## 我们到底在解决什么问题

我们先不急着上定义。想一个很常见的场景:你写了一个告警系统,运维同学找到你说,他们希望能在配置文件里写规则,比如 `cpu > 80 and mem > 90`,然后你的程序去解析这条规则、对实时指标求值,决定要不要报警。你当然可以直接在配置里塞一堆 `if` 分支,用枚举值硬编码条件类型——但很快你就会发现规则越加越多:今天要 `and`,明天要 `or`,后天有人想写 `cpu > 80 and (mem > 90 or disk > 95)`,再后天还有人想加上变量、函数、四则运算。硬编码的分支路数撑不了几轮。

解释器模式要解决的就是这类需求:**当你的程序需要理解并执行一种文本化的小型语言(DSL)时,怎么把这门语言的语法和语义,以面向对象、可扩展的方式编码进程序里**。规则引擎的过滤条件、配置里的表达式、搜索查询语法、计算器——它们都有「把一串文本按既定规则解释成一个结果」的天然诉求。

这里有一点很容易误会:解释器模式并不等于「写一个完整的编程语言」(那叫编译器/解释器工程,是另一个量级的事)。GoF 在《设计模式》里把它定位得很克制——它适合的是**语法规则明确、结构相对简单、重复执行频率不高**的小型 DSL。Python 解释器确实是解释器模式思想的巨型应用,但你在日常工程里要写的,更多是一个能解析几十条规则的迷你语言。

接下来我们就一步步来,从最蠢的写法开始,看看每一步为什么不够,最后逼出一个结构清楚、能扩展的现代 C++ 解释器骨架。

## 第一步:最原始的写法——直接调库(看起来够了,其实没开始)

我们先把场景缩到最小:输入是一个十进制整数字符串,我们要把它解释成一个整数值。很多朋友第一反应是这样的:

```cpp
int main() {
    std::string input = "12345";
    int value = std::stoi(input);
    std::cout << value << "\n";  // 12345
}
```

说实话,这真的没什么可挑剔的——单看「把数字字符串变成数字」这一件事,标准库已经包办了,没有任何理由去自己造轮子。我们列出这一步,是为了说明一个关键的认知:**解释器模式解决的不是「解析一个数字」,而是「解析一种有结构的语言」**。当你只有「一个数字」时,`std::stoi` / `std::from_chars` 就是终点;但当你的输入开始有运算符、优先级、括号、嵌套时,单次字符串扫描就不够了,你需要把「这段文本在语法上由哪些零件组成」这件事显式地表达出来。

所以下一步,我们先用解释器模式的眼光,重新看一遍「解析一个数字」这件小事——重点不是结果,而是它教给我们的那个抽象。

## 第二步:把语法零件「类化」——interpret 接口

解释器模式的核心动作只有一个:**把语法里的每一种构成元素,对应到程序里的一个类,每个类实现一个统一的「解释」方法**。这门小语言里最小的零件是「一个数字」(在文法里叫*终结符*,terminal),那我们就给它写一个类:

```cpp
#include <charconv>
#include <memory>
#include <stdexcept>
#include <string>

struct Context {
    std::string input;
    explicit Context(std::string s) : input(std::move(s)) {}
};

struct Expression {
    virtual ~Expression() = default;
    virtual long long interpret(const Context& ctx) const = 0;
};

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
```

你会发现这里做了三件事,我们要逐个讲清楚它们各自的意义。

第一,定义了一个 `Context`。它表面上只是包了一下输入字符串,但在解释器模式里,`Context` 是一个正经的角色:它承载「解释过程中需要被读写的全局状态」——可能是输入流、当前位置、变量表、错误信息。一开始它很薄,随着语言变复杂,它会越来越像一个「解释器的运行时环境」。把它单独拎出来,是为了让每个表达式节点都通过同一个入口拿上下文,而不是各自去读全局变量。

第二,定义了 `Expression` 这个抽象基类,核心是一个纯虚 `interpret`。这一行就是整个模式的命门:**所有语法零件,不管多复杂,对外都只暴露同一个接口**。`Number` 实现它,后面要加的加减乘除、括号、变量,也都实现它。调用方拿到的永远是一个 `Expression&`,它不需要知道手里这棵子树到底是数字还是二元运算。

第三,`Number` 用 `std::from_chars` 真正去解析。这里我们顺手把一个坑修掉。`std::from_chars` 的签名是 `from_chars(first, last, value)`,其中 `last` 是 *past-the-end* 迭代器(指向结尾的下一个位置),不是「要解析的字符个数」。有些资料会把 end 参数写成指针加偏移再硬减某个量,比如 `str + ctx.input.size() - pos`——这个写法在这里凑巧能对上,但它把「past-the-end」这个语义藏起来了,容易让人误以为第二个参数是长度。我们直接写 `ctx.input.data() + ctx.input.size()`,语义清楚:从 `pos` 开始,一直解析到字符串末尾,遇到第一个非数字字符就停。这才是 `from_chars` 设计上最舒服的用法。

我们先把这一步跑起来,确认 `interpret` 这条路是通的:

```cpp
int main() {
    Context ctx("12345");
    auto expr = std::make_unique<Number>(0);
    std::cout << expr->interpret(ctx) << "\n";  // 12345
}
```

到这里你可能要问了:绕这么大一圈,不还是为了得到一个 `12345` 吗?对,如果这门语言永远只有一个数字,那解释器模式就是杀鸡用牛刀。**这一步的真正价值在于它立下了两条规矩**——一个 `Context`,一个统一的 `interpret` 接口。接下来只要往这门语言里加东西,你都是顺着这两条规矩走,而不是另起炉灶。

## 第三步:从「解释」到「求值」——引入 AST

接下来问题来了。我们要让这门语言支持 `+ - * /` 和括号,也就是说输入变成 `1+2*3` 这种带结构的表达式。这时 `interpret(Context&)` 这个接口就开始别扭了:一个二元加法节点,它手里没有左右两个操作数的文本,它有的是「两个子表达式」,它的「解释」动作其实是「先解释左边、再解释右边,然后把两个结果加起来」。

这就引出了解释器模式在工程里的真正形态:**先把文本解析成一棵抽象语法树(AST),再对这棵树递归求值**。AST 的每个节点都是一个 `Expression`,叶子是数字(`NumberNode`),非叶子是二元运算(`BinaryNode`)。求值就是从叶子往根递归。

这里有一个细节值得停下来想一下。GoF 原版里,所有节点共享的是 `interpret(Context&)`;但在「先建 AST 再求值」的工程化做法里,树一旦建好,上下文里那点输入字符串的信息已经被消费完了,每个节点自己手里就攥着它需要的全部信息(数字攥着值,二元节点攥着操作符和两个子节点)。所以现代实现里,求值接口通常不传 `Context`,而是 `evaluate()` 直接返回值。我们这里就采用 `evaluate()` 这种更贴合 AST 的写法,它和 `interpret(Context&)` 本质同源——都是「解释自己这个语法片段」,只是一个喂全局上下文,一个自给自足。

先定义节点。我们用一个 `Node` 抽象基类,两种具体节点:`NumberNode`(终结符)和 `BinaryNode`(非终结符,负责 `+ - * /`)。`BinaryNode` 用两个 `std::unique_ptr<Node>` 持有左右子树——这里 `unique_ptr` 的所有权语义正好对应 AST 的树形结构:父节点独占子节点,整棵树销毁时,递归析构会自动把所有子节点连同释放掉,不用我们手写一行释放代码。

```cpp
#include <memory>
#include <stdexcept>

struct Node {
    virtual ~Node() = default;
    virtual long long evaluate() const = 0;
};

struct NumberNode : Node {
    long long value;
    explicit NumberNode(long long v) : value(v) {}
    long long evaluate() const override { return value; }
};

struct BinaryNode : Node {
    char op;
    std::unique_ptr<Node> left, right;
    BinaryNode(char o, std::unique_ptr<Node> l, std::unique_ptr<Node> r)
        : op(o), left(std::move(l)), right(std::move(r)) {}

    long long evaluate() const override {
        long long a = left->evaluate();
        long long b = right->evaluate();
        switch (op) {
            case '+': return a + b;
            case '-': return a - b;
            case '*': return a * b;
            case '/':
                if (b == 0) throw std::runtime_error("division by zero");
                return a / b;
        }
        throw std::runtime_error("unknown operator");
    }
};
```

你看,`BinaryNode::evaluate()` 干的事就是「先求左、再求右、再按操作符合并」——这正是递归的天然形态。一棵树不管多深,从根调一次 `evaluate()`,就会一路递归到叶子,再把结果层层合并回来。这就是 AST 求值的全部魔法:它把「带优先级和括号的求值」这个看似复杂的问题,拆成了一堆「我自己只负责合并两个子结果」的简单问题。

## 第四步:把文本变成树——递归下降解析器

现在我们手里有了一个能求值的 AST 类体系,但还缺最关键的一环:**怎么把 `"1+2*3"` 这样的文本,变成上面那棵树**。这件事交给一个 `Parser` 来做,我们用最经典也最直白的**递归下降(recursive descent)**写法。

递归下降的核心思想,是把文法规则一一对应到一组互相调用的函数。我们这门小语言的文法(用一种近似 BNF 的写法)是这样的:

```text
expression := term   (('+' | '-') term)*
term       := factor (('*' | '/') factor)*
factor     := number | '(' expression ')'
number     := ['-'? ] digit+
```

这三层 `expression` / `term` / `factor` 不是随便分的,它们正好对应运算符的优先级:`factor` 优先级最高(数字本身或括号包起来的整个表达式),`term` 处理乘除,`expression` 处理加减。**优先级在递归下降里,是通过「谁调用谁」的层次关系自然表达出来的**——加减在最外层,它会去调 `term`,而 `term` 会去调 `factor`;这意味着解析加号之前,乘除早就被更里层的 `term` 抓走了。括号则是通过 `factor` 里的递归 `parse_expression()` 实现:遇到 `(`,就跳进去重新跑一遍完整的表达式解析,直到碰上 `)`。

我们把这套机制写成代码:

```cpp
#include <cctype>
#include <memory>
#include <stdexcept>
#include <string>

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
        while (pos_ < input_.size() &&
               std::isspace(static_cast<unsigned char>(input_[pos_]))) {
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
        if (pos_ >= input_.size() ||
            !std::isdigit(static_cast<unsigned char>(input_[pos_]))) {
            throw std::runtime_error("expected number");
        }
        long long val = 0;
        while (pos_ < input_.size() &&
               std::isdigit(static_cast<unsigned char>(input_[pos_]))) {
            val = val * 10 + (input_[pos_] - '0');
            ++pos_;
        }
        return std::make_unique<NumberNode>(neg ? -val : val);
    }

    std::unique_ptr<Node> parse_factor() {
        skip_spaces();
        if (pos_ < input_.size() && input_[pos_] == '(') {
            ++pos_;  // consume '('
            auto node = parse_expression();
            skip_spaces();
            if (pos_ >= input_.size() || input_[pos_] != ')') {
                throw std::runtime_error("missing )");
            }
            ++pos_;  // consume ')'
            return node;
        }
        return parse_number();
    }

    std::unique_ptr<Node> parse_term() {
        auto node = parse_factor();
        while (true) {
            skip_spaces();
            if (pos_ < input_.size() &&
                (input_[pos_] == '*' || input_[pos_] == '/')) {
                char op = input_[pos_++];
                auto rhs = parse_factor();
                node = std::make_unique<BinaryNode>(op, std::move(node),
                                                    std::move(rhs));
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
            if (pos_ < input_.size() &&
                (input_[pos_] == '+' || input_[pos_] == '-')) {
                char op = input_[pos_++];
                auto rhs = parse_term();
                node = std::make_unique<BinaryNode>(op, std::move(node),
                                                    std::move(rhs));
            } else {
                break;
            }
        }
        return node;
    }
};
```

这里有两处设计上的取舍我们要专门拎出来讲,因为它们恰好是初学解释器模式最容易栽的地方。

**第一处是 `parse_term` / `parse_expression` 里的那个 `while` 循环。** 左结合性是靠这个循环保证的。以 `1-2-3` 为例,如果用朴素递归(每层只递归一次),你会得到 `(1-(2-3)) = 2`,这是右结合,数学上是错的;而循环写法把左侧不断「吃进来」重组,最终得到 `((1-2)-3) = -4`,这才是正确的左结合。加减乘除在数学上都是左结合的,所以这两个层都必须用循环而不是朴素右递归。这不是「随便怎么写都行」的细节,而是文法设计本身的一部分。

**第二处是 `parse_number` 里的那个一元负号 `neg`。** 它看起来人畜无害,但在这个文法里其实是个小坑。我们在 `factor` 这一层允许数字自带负号,意味着 `1--2` 这种写法会被解析成 `1 - (-2) = 3`。这在「只求值」的玩具场景里问题不大,但严格说它破坏了 `factor := number | '(' expression ')'` 这条文法规则——负号本该是一元运算符,应该有自己的文法层级(比如 `factor := '-' factor | atom`),而不是偷偷塞进 `number` 里。我们这里为了代码紧凑做了简化,但你心里要清楚:**正经的文法,一元负号应该单独立一层**,否则错误信息和 `1 - - 2` 这类边界输入会变得很迷。

## 这里先验证一下:优先级和错误处理真的对吗

口说无凭,我们把这几个典型输入喂给这套解析器,看看它到底建出了什么树、求出了什么值。编译跑一把(优先级、括号、多层嵌套、除零、缺括号各覆盖一遍):

```cpp
#include <iostream>
#include <vector>

int main() {
    std::vector<std::string> tests = {
        "1+2*3",            // 期望 7：验证 * 优先于 +
        "(1+2)*3",          // 期望 9：验证括号
        "10 - 4 / 2",       // 期望 8：验证 / 优先于 -
        "(2+3)*(4-1)",      // 期望 15
        "100",              // 期望 100：纯数字
        "2 * (3 + 4) * 5"   // 期望 70：多层
    };
    for (const auto& t : tests) {
        try {
            Parser p(t);
            auto ast = p.parse();
            std::cout << "\"" << t << "\" = " << ast->evaluate() << "\n";
        } catch (const std::exception& e) {
            std::cout << "\"" << t << "\" -> ERROR: " << e.what() << "\n";
        }
    }
}
```

编译执行的真实终端输出(`g++ 16.1.1` + `-std=c++23 -O2`):

```sh
$ g++ -std=c++23 -O2 -Wall -Wextra interpreter_verify.cpp -o interpreter_verify
$ ./interpreter_verify
"1+2*3" = 7
"(1+2)*3" = 9
"10 - 4 / 2" = 8
"(2+3)*(4-1)" = 15
"100" = 100
"2 * (3 + 4) * 5" = 70
```

优先级和括号都按数学约定来:`1+2*3` 先算 `2*3` 得 `7` 而不是顺序算成 `9`;多层 `2*(3+4)*5` 得 `70`。我们再把错误路径也跑一遍:

```sh
$ # 在程序里追加这两个用例
[divzero] 1/0 -> ERROR: division by zero
[syntax] (1+2 -> ERROR: missing )
```

除零被 `BinaryNode::evaluate()` 拦住抛异常,缺右括号被 `parse_factor()` 拦住抛异常——这两条错误路径都不会让程序静默给出错误结果。到这里我们就有了一个结构清楚、能扩展、错误处理也算体面的迷你解释器。

## 第五步:还能再现代一点吗——`std::variant` + `std::visit`

到这里我们用的还是经典 GoF 的那套虚函数继承(`Node` 基类 + 两个派生类)。这套写法清楚归清楚,但它有一个躲不掉的开销:每个节点都是一个独立堆分配的对象,一棵深表达式树意味着几十次 `new`。对于「解析一次、求值一次」的 DSL,这点开销完全可以接受;但如果你要把同一棵 AST 求值成千上万次(比如规则引擎里每秒评估几万条规则),虚函数派发和分散的堆分配就开始咬性能了。

现代 C++ 给了我们另一条路:**用 `std::variant` 把所有节点类型塞进一个带标签的联合体,用 `std::visit` 做派发**。这样一棵树就是一个 `std::vector` 里的连续节点,派发走的是 `visit` 的模板生成的跳表,不用虚函数表,缓存也友好得多。大致长这样(只示意节点定义,不展开完整的 variant 解析器):

```cpp
#include <memory>
#include <variant>
#include <vector>

struct NumberTerm {
    long long value;
};

struct BinaryTerm {
    char op;
    int left_index;   // 指向 nodes 数组里的下标,不再用指针
    int right_index;
};

using Term = std::variant<NumberTerm, BinaryTerm>;

struct Ast {
    std::vector<Term> nodes;
    int root{-1};

    long long evaluate(int idx) const {
        const auto& t = nodes[idx];
        if (auto* n = std::get_if<NumberTerm>(&t)) return n->value;
        auto* b = std::get_if<BinaryTerm>(&t);
        long long l = evaluate(b->left_index);
        long long r = evaluate(b->right_index);
        switch (b->op) {
            case '+': return l + r;
            case '-': return l - r;
            case '*': return l * r;
            case '/': return r == 0 ? (throw std::runtime_error("div0")) : l / r;
        }
        throw std::runtime_error("unknown op");
    }
};
```

注意这里我们把树拍平了:节点之间不再用 `unique_ptr<Node>` 互相指,而是统一存在一个 `std::vector<Term>` 里,用整数下标互相引用。这棵「树」在内存里是连续的,递归求值时 `std::get_if` 做的是编译期类型分派,没有任何虚函数调用。代价是可读性下降一截——下标引用不如指针直观,variant 的求值代码也比虚函数版啰嗦。

什么时候该上 `variant`?这条经验你可以记下:**解析一次、求值多次** 的热路径(规则引擎、公式重算),variant 值得;**解析完就求值一次、然后丢掉** 的冷路径(读一次配置算一个结果),虚函数版更清楚,别为了「现代」而现代。

## 解释器模式的常见变种

事情到这里还没完。我们前面这套「先建 AST、再递归求值」的做法,只是解释器模式的一种形态。工程里你会遇到至少这么几种变种,得知道它们各自适合什么场景。

最经典的就是我们刚写的这种**「AST + 解释器」**:解析和求值分离,AST 是一个可以反复用的中间产物。它的好处是同一个 AST 上能挂多种操作——求值、序列化、转字节码、用访问者模式做优化遍历,互不干扰。代价是实现量最大,AST 要占内存。

第二种是**「单遍即时执行」**:解析器在解析的过程中顺手就把值算出来,根本不构造持久的 AST。比如解析到 `1+2`,立刻算成 `3`,再继续往后吃。它的好处是内存占用极低、实现也短;代价是你再也算不出第二次结果,也没法在解析后做优化或类型检查。一次性命令解析、嵌入式里内存吃紧的场景适合它。

第三种是**「词法 + 语法分层」**:在 `Parser` 前面再插一个独立的 `Lexer`,先把字符流切成 token(`NUMBER`、`PLUS`、`LPAREN`……),`Parser` 吃的是 token 流而不是原始字符。当你的语言长出字符串字面量、注释、关键字、多字符运算符时,把词法和语法分开是基本卫生——混在一起的解析器很快就会乱成一锅粥,错误定位更是噩梦。

再往性能方向走,还有**「字节码 + 虚拟机」/「JIT」**:把 AST 编译成一串扁平的字节码,甚至直接编译成机器码,然后高速执行。Lua、Python 的实现都走的是这条路。这已经远远超出 GoF 解释器模式的范畴了,但它是「DSL 需要被高频求值」时的自然演化终点。

最后,解释器模式经常和其它模式结对出现。AST 是一棵树,所以它天然是**组合模式**的实例(树形结构统一接口);想对同一棵 AST 做打印、类型检查、求值等多种操作,**访问者模式**是不二之选;想让「整数语义」和「浮点语义」可切换,**策略模式**可以把求值策略注入进去。这些组合不是装饰,而是解释器模式扩展到中等复杂度 DSL 时必然要引入的帮手。

## 解释器模式为什么「少有人写」

说实话,解释器模式是 GoF 二十三个模式里存在感最低的一个,真在工程里手写过完整解释器的人并不多。原因不复杂。

**第一,绝大多数「解析文本」的需求,都有现成轮子。** 配置文件有 JSON/TOML/YAML 解析器;正则匹配有 `<regex>` 或 RE2;SQL 查询有 sqlite;规则引擎有现成的 drools/exprtk 之类。手写解释器是最后的手段,不是第一选择。你在决定动用解释器模式之前,先问自己一句:**这个 DSL 真的非自己造不可吗?能不能用一个库 + 几个数据结构搞定?** 多数时候答案是可以的。

**第二,文法一旦变复杂,手写解析器的维护成本会陡增。** 我们这个计算器只有 `+ - * / ()`,文法三层就讲清楚了。一旦加上变量、函数调用、字符串、类型、错误恢复,递归下降的代码量会膨胀得很快,错误信息也会越来越难写准。到那个量级,正经的做法是上解析器生成工具(ANTLR、Bison)或者用 Pratt parsing / parser combinator 这类更工程化的技术,而不是继续在 GoF 解释器模式的框架里硬撑。

**第三,解释器模式本身有个性能天花板。** 经典的虚函数 + 堆分配 AST,每次求值都要走一遍虚函数派发和指针跳转,对热路径不友好。我们前面给的 `variant` 方案能缓解,但如果你真的到了需要 JIT 的量级,那就该换技术栈了。

所以什么时候解释器模式是**对**的选择?当你有一门**语法规则明确、结构简单、不会频繁膨胀、求值频率不高**的小型 DSL,而且现成库不能直接覆盖时——比如一个内部用的规则过滤表达式、一个配置文件里的简单表达式求值、一个嵌入式设备上省内存的命令解析。这种场景下,解释器模式那种「语法零件各管一摊、统一接口递归求值」的清晰结构,反而比引一个重型库更划算。

## 小结

我们把整条演进路径捋一遍:

| 阶段 | 做法 | 为什么还不够 |
|---|---|---|
| 直接调库 | `std::stoi` / `std::from_chars` | 只能解析单个值,表达不出「语言的结构」 |
| 类化终结符 | `Number : Expression`,`interpret(Context&)` | 还没有运算符,无法组合 |
| AST + 求值 | `NumberNode` / `BinaryNode` + `evaluate()` | **够用了**(结构清楚、可扩展) |
| 递归下降解析器 | `Parser` 把文本建成 AST | 文法复杂时手写维护成本陡增 |
| `variant` + `visit` | 节点拍平进 `vector`,编译期分派 | 可读性下降,只在热路径值 |

记下这几条关键结论:

- **解释器模式的命门是「统一接口」**——所有语法零件(终结符、非终结符)实现同一个 `interpret` / `evaluate`,调用方只面向接口,这正是组合模式思想的直接应用。
- **优先级在递归下降里靠函数调用层次自然表达**(`expression` 调 `term` 调 `factor`),左结合靠循环而不是朴素右递归——这不是「细节」,是文法的一部分。
- **AST 的所有权用 `std::unique_ptr` 最自然**:父节点独占子节点,递归析构自动回收;热路径再考虑 `std::variant` 拍平。
- **多数「我要解析文本」的需求,都有现成轮子**——动用解释器模式前先确认 DSL 真的非自造不可,且语法足够简单、求值频率足够低。
- 解释器模式常与**组合、访问者、策略**结对:AST 是组合模式的实例,多操作靠访问者,可切换语义靠策略。

::: tip 配套可编译工程
本节的例子在仓库 `code/volumn_codes/vol4/design-patterns/Interpreter/` 下有完整可编译工程(`.h` + main + `CMakeLists.txt`),`cmake -S . -B build && cmake --build build` 即可跑出上面这些输出。
:::

## 参考资源

- [cppreference:`std::from_chars`](https://en.cppreference.com/w/cpp/utility/from_chars)(C++17,字符串到数值解析,past-the-end 迭代器语义)
- [cppreference:`std::variant` and `std::visit`](https://en.cppreference.com/w/cpp/utility/variant/visit)(C++17,编译期类型分派)
- [cppreference:`std::unique_ptr`](https://en.cppreference.com/w/cpp/memory/unique_ptr)(AST 节点的独占所有权)
- GoF,《设计模式:可复用面向对象软件的基础》第 5 章 Interpreter(终结符 / 非终结符表达式、`interpret` 接口)
- Robert Nystrom,《Crafting Interpreters》第 6–8 章(递归下降解析、AST 的工程化讲解)
