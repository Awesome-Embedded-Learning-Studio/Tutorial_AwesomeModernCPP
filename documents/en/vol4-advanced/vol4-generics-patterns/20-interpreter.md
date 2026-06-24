---
title: 'Interpreter Pattern: Embedding a Little Language into Your Program'
description: Starting with the most straightforward `std::stoi`, we will progressively
  derive the `interpret` interface and AST, implement a recursive descent calculator
  that supports `+`, `-`, `*`, `/`, and parentheses, and clarify when we should absolutely
  avoid using this pattern.
chapter: 11
order: 20
tags:
- host
- cpp-modern
- intermediate
- 解释器模式
difficulty: intermediate
platform: host
cpp_standard:
- 11
- 17
- 20
reading_time_minutes: 22
related:
- 单例模式:从注释约束到 Meyer's Singleton
prerequisites:
- 'Chapter 6: 类与对象'
- 'Chapter 9: 智能指针与所有权'
translation:
  source: documents/vol4-advanced/vol4-generics-patterns/20-interpreter.md
  source_hash: 09eec0ea013589ea48491a6d0f95eb76279952dc43020f6b682783eedc45fae4
  translated_at: '2026-06-24T01:04:46.131281+00:00'
  engine: anthropic
  token_count: 4030
---
# Interpreter Pattern: Embedding a Mini-Language into Your Program

## What Problem Are We Actually Solving?

Let's hold off on the definitions for a moment. Consider a common scenario: you have written an alerting system, and the operations team asks you to support rules in the configuration file, such as `cpu > 80 and mem > 90`. Your program needs to parse this rule, evaluate it against real-time metrics, and decide whether to trigger an alarm. You could, of course, stuff a bunch of `if` branches into the configuration and hardcode condition types using enumerations. However, you will soon find that the rules keep piling up: today it's `and`, tomorrow it's `or`, the day after someone wants `cpu > 80 and (mem > 90 or disk > 95)`, and eventually, they will want to add variables, functions, and arithmetic operations. Hardcoded branches won't survive many iterations.

The Interpreter pattern solves exactly this class of requirements: **when your program needs to understand and execute a textual mini-language (DSL), how do you encode the syntax and semantics of that language into the program in an object-oriented, extensible way?** Filter conditions for rule engines, expressions in configurations, search query syntax, and calculators—they all share the natural need to "interpret a string of text according to established rules to produce a result."

It is easy to misunderstand this point: the Interpreter pattern does not equate to "writing a complete programming language" (that is called compiler or interpreter engineering, which is a completely different magnitude of work). The Gang of Four (GoF) positioned it very modestly in *Design Patterns*—it is suitable for **small DSLs with clear grammatical rules, relatively simple structure, and low execution frequency**. The Python interpreter is indeed a colossal application of the Interpreter pattern's philosophy, but what you will likely write in daily engineering is a mini-language capable of parsing a few dozen rules.

Next, we will go through this step-by-step. We will start with the dumbest approach, see why it falls short at each stage, and finally force out a well-structured, extensible modern C++ interpreter skeleton.

## Step 1: The Most Primitive Approach — Direct Library Calls (Looks Like Enough, But We Haven't Really Started)

Let's shrink the scenario to the minimum: the input is a decimal integer string, and we need to interpret it as an integer value. Many people's first reaction looks like this:

```cpp
int main() {
    std::string input = "12345";
    int value = std::stoi(input);
    std::cout << value << "\n";  // 12345
}
```

Honestly, there is nothing to criticize here. If we look strictly at "converting a numeric string to a number," the standard library handles it perfectly, and there is absolutely no reason to reinvent the wheel. We list this step to illustrate a key insight: **the Interpreter pattern does not solve "parsing a number," but rather "parsing a structured language."** When you only have "a number," `std::stoi` / `std::from_chars` is the final answer. However, when your input starts to include operators, precedence, parentheses, and nesting, a single string scan is no longer sufficient. You need to explicitly represent "what syntactic components make up this text."

So, in the next step, we will revisit the trivial task of "parsing a number" through the lens of the Interpreter pattern. The focus is not on the result, but on the abstraction it teaches us.

## Step 2: Turning Syntax Components into Classes — The `interpret` Interface

There is only one core action in the Interpreter pattern: **map every constituent element in the grammar to a class in the program, where each class implements a unified "interpret" method.** The smallest component in this mini-language is "a number" (called a *terminal* in grammar theory), so let's write a class for it:

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

You will notice that we have accomplished three things here, and we need to clarify the significance of each one individually.

First, we defined a `Context`. On the surface, it simply wraps the input string, but in the Interpreter pattern, `Context` plays a formal role: it carries the "global state that needs to be read from or written to during interpretation"—which could include the input stream, current position, symbol table, or error information. It starts out thin, but as the language becomes more complex, it will increasingly resemble a "runtime environment for the interpreter." We isolate it to ensure that every expression node accesses context through the same entry point, rather than each one reading global variables directly.

Second, we defined the `Expression` abstract base class, centered around a pure virtual `interpret` function. This line is the crux of the entire pattern: **all syntactic components, no matter how complex, expose the same interface to the outside world.** `Number` implements it, and the addition, subtraction, multiplication, division, parentheses, and variables we will add later will all implement it as well. The caller always holds an `Expression&`, and it does not need to know whether the subtree in hand is a number or a binary operation.

Third, `Number` uses `std::from_chars` to perform the actual parsing. Here, we also fix a potential pitfall. The signature of `std::from_chars` is `from_chars(first, last, value)`, where `last` is a *past-the-end* iterator (pointing to the position immediately following the end), not "the number of characters to parse." Some resources might write the end parameter as a pointer plus an offset minus a hard-coded quantity, like `str + ctx.input.size() - pos`—this approach happens to work here, but it obscures the "past-the-end" semantics, leading people to mistakenly believe the second parameter is a length. We write `ctx.input.data() + ctx.input.size()` directly to make the semantics clear: start from `pos` and parse until the end of the string, stopping at the first non-numeric character. This is the most idiomatic usage intended by the design of `from_chars`.

Let's run this step first to confirm that the `interpret` path works:

```cpp
int main() {
    Context ctx("12345");
    auto expr = std::make_unique<Number>(0);
    std::cout << expr->interpret(ctx) << "\n";  // 12345
}
```

At this point, you might ask: "After going in such a huge circle, aren't we just trying to get a `12345`?" Yes, if this language were to consist of only a single number forever, the Interpreter pattern would indeed be overkill (using a sledgehammer to crack a nut). **The true value of this step lies in establishing two ground rules**—a `Context` and a unified `interpret` interface. From now on, as we add features to this language, we will follow these two rules rather than reinventing the wheel.

## Step 3: From "Interpretation" to "Evaluation" — Introducing the AST

Now, the next challenge arises. We want this language to support `+`, `-`, `*`, `/`, and parentheses, meaning the input becomes structured expressions like `1+2*3`. At this point, the `interpret(Context&)` interface starts to feel awkward: a binary addition node doesn't hold the text for the left and right operands; instead, it holds "two sub-expressions." Its "interpretation" action is actually "interpret the left side, then the right side, and finally add the two results together."

This leads us to the true form of the Interpreter pattern in engineering: **parse the text into an Abstract Syntax Tree (AST) first, then recursively evaluate this tree**. Each node in the AST is an `Expression`. Leaves are numbers (`NumberNode`), and non-leaves are binary operations (`BinaryNode`). Evaluation is simply a recursion from the leaves to the root.

Here is a detail worth pausing to consider. In the original GoF (Gang of Four) book, all nodes share an `interpret(Context&)` interface. However, in the engineering practice of "build AST first, then evaluate," once the tree is built, the input string information in the context has been consumed. Each node holds all the information it needs (numbers hold their values, binary nodes hold the operator and two child nodes). Therefore, in modern implementations, the evaluation interface usually does not pass a `Context`; instead, `evaluate()` returns the value directly. We will adopt the `evaluate()` approach here, as it fits the AST better. It shares the same origin as `interpret(Context&)`—both "interpret this syntax fragment"—except one feeds on a global context while the other is self-sufficient.

Let's define the nodes first. We use an abstract base class `Node` and two concrete node types: `NumberNode` (terminal) and `BinaryNode` (non-terminal, handling `+ - * /`). `BinaryNode` uses two `std::unique_ptr<Node>` to hold the left and right subtrees. Here, the ownership semantics of `unique_ptr` perfectly match the tree structure of the AST: parent nodes exclusively own child nodes. When the entire tree is destroyed, the recursive destructor will automatically release all child nodes, so we don't need to write a single line of cleanup code.

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

You see, what `BinaryNode::evaluate()` does is "evaluate the left side, then the right side, and finally merge them using the operator"—this is the natural form of recursion. No matter how deep the tree is, calling `evaluate()` once from the root triggers recursion all the way down to the leaves, and then the results are merged back up layer by layer. This is the entire magic behind AST evaluation: it breaks down the seemingly complex problem of "evaluating expressions with precedence and parentheses" into a bunch of simple problems where "I am only responsible for merging two sub-results."

## Step 4: Turning Text into Trees—Recursive Descent Parser

Now that we have an AST class hierarchy capable of evaluation, we are missing the most critical piece: **how do we turn text like `"1+2*3"` into the tree structure described above?** We delegate this task to a `Parser`, using the most classic and straightforward **recursive descent** approach.

The core idea of recursive descent is to map grammar rules one-to-one to a set of mutually calling functions. The grammar for our mini-language (written in a BNF-like notation) looks like this:

```text
expression := term   (('+' | '-') term)*
term       := factor (('*' | '/') factor)*
factor     := number | '(' expression ')'
number     := ['-'? ] digit+
```

These three layers—`expression`, `term`, and `factor`—are not arbitrary; they correspond exactly to operator precedence: `factor` has the highest precedence (numbers themselves or entire expressions wrapped in parentheses), `term` handles multiplication and division, and `expression` handles addition and subtraction. **In recursive descent, precedence is naturally expressed through the hierarchy of "who calls whom"**—addition and subtraction are at the outermost level, calling `term`, which in turn calls `factor`. This means that by the time we parse an addition or subtraction sign, the multiplication or division has already been captured by the deeper `term` layer. Parentheses are implemented via the recursive `parse_expression()` call within `factor`: when encountering `(`, we jump back in and run the full expression parsing routine again until we hit `)`.

Let's write this mechanism as code:

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

There are two specific design trade-offs we need to highlight, as they happen to be the most common pitfalls for beginners learning the interpreter pattern.

**The first is the `while` loop inside `parse_term` / `parse_expression`.** Left-associativity is guaranteed by this loop. Taking `1-2-3` as an example, if we used naive recursion (recursing only once per level), we would get `(1-(2-3)) = 2`, which is right-associative and mathematically incorrect. The loop approach, however, continuously "consumes" the left-hand side to restructure it, ultimately yielding `((1-2)-3) = -4`, which is the correct left-associative result. Since addition, subtraction, multiplication, and division are all left-associative in mathematics, both of these levels must use a loop rather than naive right recursion. This isn't a trivial detail where "anything goes"; it is an intrinsic part of the grammar design.

**The second is the unary minus `neg` inside `parse_number`.** It looks harmless enough, but in this grammar, it's actually a small trap. We allow numbers to carry their own negative sign at the `factor` level, meaning an expression like `1--2` is parsed as `1 - (-2) = 3`. While this is acceptable in a toy scenario that "just evaluates," strictly speaking, it violates the grammar rule `factor := number | '(' expression ')'`. The minus sign should be a unary operator with its own grammar level (e.g., `factor := '-' factor | atom`), rather than being sneaked into `number`. We made this simplification for code compactness, but keep in mind: **in a proper grammar, the unary minus should have its own layer**. Otherwise, error messages and edge-case inputs like `1 - - 2` will become quite confusing.

## Let's Verify This: Are Precedence and Error Handling Correct?

Talk is cheap. Let's feed several typical inputs into this parser to see what tree structures it builds and what values it evaluates. We'll compile and run a test (covering precedence, parentheses, multi-level nesting, division by zero, and missing parentheses):

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

The actual terminal output from compilation and execution (`g++ 16.1.1` + `-std=c++23 -O2`):

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

Precedence and parentheses follow mathematical conventions: in `1+2*3`, `2*3` is evaluated first to get `7`, rather than evaluating sequentially to get `9`. For multiple layers, `2*(3+4)*5` results in `70`. Let's walk through the error path as well:

```sh
$ # 在程序里追加这两个用例
[divzero] 1/0 -> ERROR: division by zero
[syntax] (1+2 -> ERROR: missing )
```

Division by zero is caught by `BinaryNode::evaluate()` and throws an exception, while missing right parentheses are caught by `parse_factor()` and throw an exception. These two error paths prevent the program from silently producing incorrect results. At this point, we have a mini-interpreter with a clear structure, extensibility, and decent error handling.

## Step 5: Can We Make It Even More Modern? — `std::variant` + `std::visit`

So far, we have been using the classic GoF (Gang of Four) approach of virtual function inheritance (a `Node` base class with two derived classes). While this approach is clear, it carries an unavoidable overhead: each node is a separate heap-allocated object, meaning a deep expression tree results in dozens of `new` operations. For a DSL that is "parsed once, evaluated once," this overhead is perfectly acceptable. However, if you need to evaluate the same AST thousands or tens of thousands of times (for example, evaluating tens of thousands of rules per second in a rule engine), virtual function dispatch and scattered heap allocations will start to bite into performance.

Modern C++ offers us another path: **use `std::variant` to stuff all node types into a tagged union, and use `std::visit` for dispatch**. This way, a tree is just a contiguous block of nodes in a `std::vector`, dispatch uses the jump table generated by the `visit` template instead of a virtual function table, and it is much more cache-friendly. It looks roughly like this (illustrating only the node definition, not the full variant parser):

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

Note that we have flattened the tree here: nodes no longer point to each other via `unique_ptr<Node>`, but are stored uniformly in a `std::vector<Term>`, referencing each other via integer indices. This "tree" is contiguous in memory. During recursive evaluation, `std::get_if` performs compile-time type dispatch without any virtual function calls. The cost is a significant drop in readability—index references are less intuitive than pointers, and the `variant` evaluation code is more verbose than the virtual function version.

When should you use `variant`? You can keep this rule of thumb: for **hot paths where you parse once and evaluate multiple times** (rule engines, formula recalculation), `variant` is worth it. For **cold paths where you parse, evaluate once, and then discard** (reading a configuration once to compute a result), the virtual function version is clearer. Don't go "modern" just for the sake of it.

## Common Variations of the Interpreter Pattern

We aren't done yet. The "build AST first, then recursive evaluation" approach we discussed is just one form of the Interpreter pattern. In practice, you will encounter at least these variations, and you need to know which scenarios they fit.

The most classic is the **"AST + Interpreter"** we just wrote: parsing and evaluation are separated, and the AST is a reusable intermediate product. Its benefit is that you can hang multiple operations on the same AST—evaluation, serialization, bytecode generation, optimization passes using the Visitor pattern—without interference. The cost is the highest implementation overhead, and the AST consumes memory.

The second is **"Single-pass Immediate Execution"**: The parser calculates the value on the fly while parsing, without constructing a persistent AST at all. For example, when parsing `1+2`, it immediately calculates `3` and continues consuming input. Its benefits are extremely low memory usage and short implementation; the cost is that you can never calculate a second result, nor can you perform optimizations or type checking after parsing. This suits one-shot command parsing or memory-constrained embedded scenarios.

The third is **"Lexical + Syntactic Layering"**: Inserting an independent `Lexer` before the `Parser` to slice the character stream into tokens (`NUMBER`, `PLUS`, `LPAREN`...), so the `Parser` consumes a token stream instead of raw characters. When your language grows string literals, comments, keywords, and multi-character operators, separating lexing and parsing is basic hygiene—a mixed parser quickly becomes a mess, and error localization becomes a nightmare.

Moving further towards performance, there are **"Bytecode + VM" / "JIT"** approaches: compiling the AST into a flat string of bytecode, or even directly into machine code, for high-speed execution. Implementations of Lua and Python take this route. This goes far beyond the scope of the GoF Interpreter pattern, but it is the natural evolutionary endpoint when a DSL requires high-frequency evaluation.

Finally, the Interpreter pattern often appears in tandem with other patterns. An AST is a tree, so it is naturally an instance of the **Composite Pattern** (unified interface for tree structures); to perform printing, type checking, and evaluation on the same AST, the **Visitor Pattern** is the best choice; to make "integer semantics" and "floating-point semantics" switchable, the **Strategy Pattern** can inject evaluation strategies. These combinations aren't decoration; they are necessary helpers when extending the Interpreter pattern to moderately complex DSLs.

## Why the Interpreter Pattern is "Rarely Written"

Honestly, the Interpreter pattern is the one with the lowest presence among the twenty-three GoF patterns. Few people actually write a complete interpreter by hand in real engineering. The reasons aren't complex.

**First, the vast majority of "text parsing" needs have existing wheels.** Configuration files have JSON/TOML/YAML parsers; regex matching has `<regex>` or RE2; SQL queries have sqlite; rule engines have ready-made options like drools/exprtk. Writing an interpreter by hand is a last resort, not a first choice. Before deciding to use the Interpreter pattern, ask yourself: **Is this DSL really necessary? Can I get away with a library + a few data structures?** Most of the time, the answer is yes.

**Second, once the grammar becomes complex, the maintenance cost of a hand-written parser increases sharply.** Our calculator only has `+ - * / ()`, and the grammar is clear in three layers. Once you add variables, function calls, strings, types, and error recovery, the recursive descent code bloats quickly, and error messages become harder to write accurately. At that level, the proper approach is to use parser generators (ANTLR, Bison) or more engineering-heavy techniques like Pratt parsing or parser combinators, rather than struggling to prop it up within the GoF Interpreter framework.

**Third, the Interpreter pattern has a performance ceiling.** The classic virtual function + heap-allocated AST means every evaluation walks through virtual dispatch and pointer indirection, which is unfriendly to hot paths. The `variant` solution we provided can alleviate this, but if you really reach the level where you need JIT, it's time to switch technology stacks.

So when is the Interpreter pattern the **right** choice? When you have a small DSL with **clear syntax rules, simple structure, infrequent expansion, and low evaluation frequency**, and existing libraries don't cover it directly—such as an internal rule filtering expression, a simple expression evaluation in a config file, or a memory-saving command parser on an embedded device. In these scenarios, the clear structure of the Interpreter pattern—"syntax parts each manage their own share, unified interface recursive evaluation"—is more cost-effective than introducing a heavy library.

## Summary

Let's review the entire evolution path:

| Stage | Approach | Why it falls short |
|---|---|---|
| Direct Library Call | `std::stoi` / `std::from_chars` | Can only parse single values, cannot express "language structure" |
| Classified Terminals | `Number : Expression`, `interpret(Context&)` | No operators yet, cannot combine |
| AST + Evaluation | `NumberNode` / `BinaryNode` + `evaluate()` | **Sufficient** (clear structure, extensible) |
| Recursive Descent Parser | `Parser` builds AST from text | Hand-written maintenance cost spikes when grammar gets complex |
| `variant` + `visit` | Flatten nodes into `vector`, compile-time dispatch | Readability drops, only worth it on hot paths |

Keep these key conclusions in mind:

- **The soul of the Interpreter pattern is the "unified interface"**—all syntax parts (terminals, non-terminals) implement the same `interpret` / `evaluate`, and the caller only faces the interface. This is a direct application of the Composite pattern concept.
- **Precedence is naturally expressed by function call hierarchy in recursive descent** (`expression` calls `term` calls `factor`), and left-associativity relies on loops rather than naive right recursion—this isn't a "detail," it's part of the grammar.
- **AST ownership is most natural with `std::unique_ptr`**: parent nodes own child nodes, recursive destruction automatically reclaims memory; consider `std::variant` flattening for hot paths.
- **Most "I need to parse text" requirements have existing wheels**—before using the Interpreter pattern, confirm the DSL really must be custom, and the syntax is simple enough and evaluation infrequent enough.
- The Interpreter pattern often pairs with **Composite, Visitor, Strategy**: AST is an instance of Composite, multiple operations use Visitor, and switchable semantics use Strategy.

::: tip Companion Compilable Project
The examples in this section have a complete compilable project in the repository at `code/volumn_codes/vol4/design-patterns/Interpreter/` (`.h` + main + `CMakeLists.txt`). Run `cmake -S . -B build && cmake --build build` to see the outputs shown above.
:::

## References

- [cppreference: `std::from_chars`](https://en.cppreference.com/w/cpp/utility/from_chars) (C++17, string to number parsing, past-the-end iterator semantics)
- [cppreference: `std::variant` and `std::visit`](https://en.cppreference.com/w/cpp/utility/variant/visit) (C++17, compile-time type dispatch)
- [cppreference: `std::unique_ptr`](https://en.cppreference.com/w/cpp/memory/unique_ptr) (Exclusive ownership of AST nodes)
- GoF, *Design Patterns: Elements of Reusable Object-Oriented Software*, Chapter 5 Interpreter (Terminal / Non-terminal Expressions, `interpret` interface)
- Robert Nystrom, *Crafting Interpreters*, Chapters 6–8 (Recursive descent parsing, engineering discussion of ASTs)
