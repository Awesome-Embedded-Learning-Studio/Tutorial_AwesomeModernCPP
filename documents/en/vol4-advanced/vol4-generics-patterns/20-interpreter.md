---
title: 'Interpreter Pattern: Stuffing a Little Language into Your Program'
description: 'Starting from the most naive "just call std::stoi" version, we squeeze out the interpret interface and an AST step by step, build a recursive-descent calculator that handles + - * / and parentheses, and close with a clear-eyed look at when you should not use this pattern at all'
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
  - 'Singleton Pattern: From Comment-Only Constraints to Meyer''s Singleton'
prerequisites:
  - 'Chapter 6: Classes and Object-Oriented Programming'
  - 'Chapter 9: Smart Pointers and Ownership'
translation:
  source: documents/vol4-advanced/vol4-generics-patterns/20-interpreter.md
  source_hash: 09eec0ea013589ea48491a6d0f95eb76279952dc43020f6b682783eedc45fae4
  translated_at: '2026-09-26T05:59:38+00:00'
  engine: anthropic
  token_count: 11000
---

# Interpreter Pattern: Stuffing a Little Language into Your Program

## What Problem Are We Actually Solving

Let's not rush to a definition. Think of a very common scenario: you've built an alerting system, and the ops folks come to you saying they'd like to write rules in a config file — something like `cpu > 80 and mem > 90` — and have your program parse the rule, evaluate it against live metrics, and decide whether to raise an alarm. Sure, you could stuff a pile of `if` branches into the config and hardcode condition types as enum values — but you'd quickly watch the rules multiply: today they want `and`, tomorrow `or`, the day after someone writes `cpu > 80 and (mem > 90 or disk > 95)`, and the day after that someone else wants variables, functions, and the four arithmetic operations. A hardcoded maze of branches won't survive many rounds of that.

That's the family of needs the Interpreter pattern addresses: **when your program has to understand and execute a small textual language (a DSL), how do you encode that language's grammar and semantics into the program in an object-oriented, extensible way?** Rule-engine filter conditions, expressions in configuration, search query syntax, calculators — they all share the same natural desire: "take this string of text and, by a fixed set of rules, interpret it into a result."

One thing is easy to misread here: the Interpreter pattern is not the same thing as "writing a full programming language" (that's compiler/interpreter engineering, an entirely different order of magnitude). GoF's positioning in *Design Patterns* is deliberately modest — the pattern fits **small DSLs with well-defined grammar rules, relatively simple structure, and low re-execution frequency**. The Python interpreter is indeed a giant application of the Interpreter pattern's ideas, but what you'll write in day-to-day engineering is more often a mini language that can parse a few dozen rules.

So let's proceed step by step, starting from the dumbest possible version, seeing exactly why each step falls short, until we've squeezed out a modern C++ interpreter skeleton with a clear, extensible structure.

## Step 1: The Most Primitive Version — Calling the Library Directly (It Looks Like Enough, but Interpretation Hasn't Even Started)

First, shrink the scenario to its minimum: the input is a decimal integer string, and we need to interpret it into an integer value. Many people's first reaction looks like this:

```cpp
int main() {
    std::string input = "12345";
    int value = std::stoi(input);
    std::cout << value << "\n";  // 12345
}
```

Honestly, there's nothing to criticize here — for the single job of "turning a numeric string into a number," the standard library already has it covered, and there's no reason whatsoever to reinvent that wheel. We include this step to establish one key realization: **the Interpreter pattern is not about "parsing one number" — it's about "parsing a language with structure."** When all you have is "a number," `std::stoi` / `std::from_chars` is the end of the road; but once your input grows operators, precedence, parentheses, and nesting, a single string scan is no longer enough — you need to state explicitly "which grammatical parts this text is composed of."

So in the next step, we'll revisit the small matter of "parsing a number" through the lens of the Interpreter pattern — the point isn't the result, but the abstraction it teaches us.

## Step 2: Turning Grammar Parts into Classes — the interpret Interface

The Interpreter pattern has exactly one core move: **map every kind of grammatical element to a class in your program, and have each class implement one unified "interpret" method.** The smallest part of our little language is "a number" (called a *terminal* in grammar terms), so let's write a class for it:

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

You'll notice three things happened here, and we need to walk through what each of them means.

First, we defined a `Context`. On the surface it just wraps the input string, but in the Interpreter pattern `Context` is a proper role: it carries "the global state that needs to be read and written during interpretation" — possibly the input stream, the current position, a variable table, error information. At first it's very thin; as the language grows more complex, it comes to look more and more like "the interpreter's runtime environment." Pulling it out separately means every expression node obtains its context through the same entry point, instead of each one going off to read global variables on its own.

Second, we defined the abstract base class `Expression`, whose core is a pure virtual `interpret`. That one line is the very lifeblood of the whole pattern: **every grammatical part, no matter how complex, exposes exactly one interface to the outside.** `Number` implements it; the addition, subtraction, multiplication, division, parentheses, and variables we'll add later will all implement it too. The caller always receives an `Expression&` and never needs to know whether the subtree in hand is a number or a binary operation.

Third, `Number` does the real parsing with `std::from_chars`. Here let's fix a pitfall in passing. `std::from_chars`'s signature is `from_chars(first, last, value)`, where `last` is a *past-the-end* iterator (pointing one position past the end), not "the number of characters to parse." Some references write the end argument as a pointer plus an offset minus some quantity, e.g. `str + ctx.input.size() - pos` — that expression happens to be correct here, but it buries the "past-the-end" semantics and makes it easy to mistake the second argument for a length. We write `ctx.input.data() + ctx.input.size()` directly, which reads clearly: starting from `pos`, parse all the way to the end of the string, stopping at the first non-digit character. That's the usage `from_chars` was designed to be most comfortable with.

Let's get this step running first and confirm the `interpret` road actually works:

```cpp
int main() {
    Context ctx("12345");
    auto expr = std::make_unique<Number>(0);
    std::cout << expr->interpret(ctx) << "\n";  // 12345
}
```

At this point you might ask: that's a huge detour just to end up with a `12345`, isn't it? Right — if this language were forever going to contain a single number, the Interpreter pattern would be a sledgehammer for a gnat. **The real value of this step is that it lays down two rules** — one `Context`, and one unified `interpret` interface. From now on, everything you add to this language follows those two rules, rather than starting from scratch with a new mechanism each time.

## Step 3: From Interpreting to Evaluating — Bringing in the AST

Now a problem shows up. We want this language to support `+ - * /` and parentheses, which means the input becomes a structured expression like `1+2*3`. At that point the `interpret(Context&)` interface starts to feel awkward: a binary addition node doesn't hold the text of its two operands — what it holds is "two sub-expressions," and its "interpret" action is really "interpret the left side, then the right side, then add the two results together."

That leads to the Interpreter pattern's real shape in engineering: **first parse the text into an abstract syntax tree (AST), then recursively evaluate that tree.** Every AST node is an `Expression`; leaves are numbers (`NumberNode`), non-leaves are binary operations (`BinaryNode`). Evaluation is simply recursion from the leaves up to the root.

There's a detail here worth pausing on. In the original GoF version, all nodes share `interpret(Context&)`; but in the engineered "build the AST first, then evaluate" approach, once the tree is built the input-string information in the context has already been fully consumed, and each node holds all the information it needs in its own hands (a number holds its value; a binary node holds its operator and two children). So in modern implementations the evaluation interface usually takes no `Context` — `evaluate()` just returns the value. We'll adopt the `evaluate()` style here since it fits the AST better; it and `interpret(Context&)` share the same essence — both are "interpret this grammatical fragment of mine" — except one is fed a global context while the other is self-sufficient.

Let's define the nodes first. We use one `Node` abstract base class and two concrete nodes: `NumberNode` (a terminal) and `BinaryNode` (a non-terminal, handling `+ - * /`). `BinaryNode` holds its left and right subtrees in two `std::unique_ptr<Node>`s — and here `unique_ptr`'s ownership semantics map exactly onto the AST's tree shape: a parent exclusively owns its children, and when the whole tree is destroyed, recursive destruction releases all the child nodes automatically, without us hand-writing a single line of release code.

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

Look at what `BinaryNode::evaluate()` does: "evaluate the left, evaluate the right, then combine per the operator" — precisely recursion's natural form. However deep the tree, one `evaluate()` call at the root recurses all the way down to the leaves, then merges the results back up layer by layer. That's the entire magic of AST evaluation: it decomposes "evaluate with precedence and parentheses," a seemingly complicated problem, into a pile of simple "I only merge two child results" problems.

## Step 4: Turning Text into a Tree — the Recursive Descent Parser

Now we have an evaluable AST class hierarchy in hand, but the most crucial link is still missing: **how do we turn text like `"1+2*3"` into the tree above?** That job goes to a `Parser`, and we'll use the most classic and most straightforward style: **recursive descent**.

The core idea of recursive descent is to map each grammar rule one-to-one onto a set of mutually calling functions. The grammar of our little language (in a BNF-like notation) looks like this:

```text
expression := term   (('+' | '-') term)*
term       := factor (('*' | '/') factor)*
factor     := number | '(' expression ')'
number     := ['-'? ] digit+
```

These three layers — `expression` / `term` / `factor` — aren't an arbitrary division; they correspond exactly to operator precedence: `factor` has the highest precedence (a bare number, or an entire expression wrapped in parentheses), `term` handles multiplication and division, `expression` handles addition and subtraction. **In recursive descent, precedence is expressed naturally through the layering of "who calls whom"** — addition and subtraction sit on the outermost layer and call `term`, which in turn calls `factor`; this means that by the time a plus sign is being parsed, multiplication and division have long since been grabbed by the deeper `term` layer. Parentheses are implemented via the recursive `parse_expression()` inside `factor`: on seeing `(`, we jump in and rerun a complete round of expression parsing until we hit `)`.

Let's write this machinery as code:

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

There are two design trade-offs in here worth pulling out for special attention, because they're precisely where beginners of the Interpreter pattern most often stumble.

**The first is that `while` loop in `parse_term` / `parse_expression`.** Left associativity is guaranteed by that loop. Take `1-2-3`: with naive recursion (each layer recursing only once), you'd get `(1-(2-3)) = 2` — right-associative, mathematically wrong; the loop version keeps "eating in" and rebuilding the left side, ultimately producing `((1-2)-3) = -4`, the correct left associativity. Addition, subtraction, multiplication, and division are all left-associative in mathematics, so both of these layers must use a loop rather than naive right recursion. This isn't a "whatever way you write it works" detail — it's part of the grammar design itself.

**The second is that unary minus `neg` in `parse_number`.** It looks harmless, but in this grammar it's actually a small trap. Allowing a number to carry its own minus sign at the `factor` layer means an input like `1--2` gets parsed as `1 - (-2) = 3`. In a toy "evaluate only" scenario that's not much of a problem, but strictly speaking it violates the grammar rule `factor := number | '(' expression ')'` — the minus sign ought to be a unary operator with its own grammar level (say `factor := '-' factor | atom`), not smuggled into `number`. We simplified here for code compactness, but be clear in your mind: **in a proper grammar, unary minus deserves its own layer**; otherwise error messages and boundary inputs like `1 - - 2` become deeply confusing.

## A Quick Verification: Are Precedence and Error Handling Actually Right

Talk is cheap, so let's feed these typical inputs to the parser and see what tree it actually builds and what values it produces. Compile and run it once (covering precedence, parentheses, multi-level nesting, division by zero, and a missing parenthesis, one each):

```cpp
#include <iostream>
#include <vector>

int main() {
    std::vector<std::string> tests = {
        "1+2*3",            // expect 7: * binds tighter than +
        "(1+2)*3",          // expect 9: parentheses
        "10 - 4 / 2",       // expect 8: / binds tighter than -
        "(2+3)*(4-1)",      // expect 15
        "100",              // expect 100: a bare number
        "2 * (3 + 4) * 5"   // expect 70: multiple levels
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

The real terminal output of compiling and running (`g++ 16.1.1` + `-std=c++23 -O2`):

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

Precedence and parentheses both follow mathematical convention: `1+2*3` computes `2*3` first, yielding `7`, rather than evaluating sequentially into `9`; the multi-level `2*(3+4)*5` yields `70`. Let's run the error paths too:

```sh
$ # append these two cases to the program
[divzero] 1/0 -> ERROR: division by zero
[syntax] (1+2 -> ERROR: missing )
```

Division by zero is caught by `BinaryNode::evaluate()`, which throws; the missing right parenthesis is caught by `parse_factor()`, which throws — neither error path lets the program silently produce a wrong result. At this point we have a mini interpreter with a clear structure, room to grow, and genuinely decent error handling.

## Step 5: Can We Get Even More Modern — `std::variant` + `std::visit`

Up to here we've been using the classic GoF virtual-function inheritance (`Node` base class + two derived classes). Clear as that style is, it carries an overhead you can't dodge: every node is an independently heap-allocated object, and a deep expression tree means dozens of `new`s. For a DSL that's "parse once, evaluate once," that overhead is perfectly acceptable; but if you're going to evaluate the same AST thousands upon thousands of times (say a rules engine evaluating tens of thousands of rules per second), virtual dispatch and scattered heap allocations start biting into performance.

Modern C++ offers us another road: **stuff all the node types into one tagged union with `std::variant`, and dispatch with `std::visit`.** The tree then becomes contiguous nodes inside a `std::vector`; dispatch goes through the jump table generated by `visit`'s templates, with no virtual function tables, and it's far friendlier to the cache. It looks roughly like this (only the node definitions as a sketch — not a full variant-based parser):

```cpp
#include <memory>
#include <variant>
#include <vector>

struct NumberTerm {
    long long value;
};

struct BinaryTerm {
    char op;
    int left_index;   // an index into the nodes array, not a pointer anymore
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

Notice we've flattened the tree here: nodes no longer point at each other via `unique_ptr<Node>`; they all live together in one `std::vector<Term>`, referencing each other by integer index. This "tree" is contiguous in memory, `std::get_if` performs compile-time type dispatch during the recursive evaluation, and there isn't a single virtual function call. The price is a real drop in readability — index references are less intuitive than pointers, and the variant evaluation code is wordier than the virtual-function version.

When should you reach for `variant`? Note this rule of thumb: on hot paths that **parse once and evaluate many times** (rules engines, formula recomputation), variant is worth it; on cold paths that **parse, evaluate exactly once, and then get thrown away** (read a config once, compute one result), the virtual-function version is clearer — don't be modern for modernity's sake.

## Common Variants of the Interpreter Pattern

We're not done yet. The "build an AST first, then recursively evaluate" approach above is just one shape the Interpreter pattern takes. In engineering you'll run into at least the variants below, and you should know which scenario each fits.

The most classic is the **"AST + interpreter"** we just wrote: parsing and evaluation are separate, and the AST is a reusable intermediate artifact. Its strength is that the same AST can carry many operations — evaluation, serialization, conversion to bytecode, optimization passes with the Visitor pattern — without interfering with one another. The cost is the largest implementation effort, plus the memory the AST occupies.

The second is **"single-pass immediate execution"**: the parser computes values on the fly as it parses and never builds a persistent AST. For example, on reaching `1+2`, it immediately computes `3` and keeps eating onward. Its strengths are extremely low memory usage and a short implementation; the costs are that you can never compute the result a second time, and you can't perform post-parse optimization or type checking. One-shot command parsing and memory-tight embedded scenarios suit it.

The third is **"separate lexer and parser layers"**: insert an independent `Lexer` in front of the `Parser`, which first chops the character stream into tokens (`NUMBER`, `PLUS`, `LPAREN`, ...), and the `Parser` then eats a token stream instead of raw characters. Once your language grows string literals, comments, keywords, and multi-character operators, splitting lexing from parsing is basic hygiene — a parser with the two mixed together turns into a tangled mess in no time, and error localization becomes a nightmare.

Pushing further toward performance, there's **"bytecode + virtual machine" / "JIT"**: compile the AST into a flat stream of bytecode, or even directly into machine code, then execute it at speed. Lua's and Python's implementations both go down this road. That's far beyond the scope of the GoF Interpreter pattern, but it's the natural evolutionary endpoint when "a DSL needs to be evaluated at high frequency."

Finally, the Interpreter pattern often appears paired with other patterns. An AST is a tree, so it's naturally an instance of the **Composite pattern** (tree structures behind a unified interface); to run printing, type checking, evaluation, and other operations over the same AST, the **Visitor pattern** is the go-to; and to make "integer semantics" and "floating-point semantics" swappable, the **Strategy pattern** can inject the evaluation strategy. These pairings aren't decoration — they're the helpers you'll inevitably bring in as the Interpreter pattern scales to a medium-complexity DSL.

## Why the Interpreter Pattern Is So Rarely Hand-Written

Honestly, the Interpreter pattern has the weakest presence of all twenty-three GoF patterns, and not many people have truly hand-written a complete interpreter in production engineering. The reasons aren't complicated.

**First, the overwhelming majority of "parse some text" needs already have ready-made wheels.** Config files have JSON/TOML/YAML parsers; regex matching has `<regex>` or RE2; SQL queries have sqlite; rules engines have off-the-shelf options like drools/exprtk. A hand-written interpreter is the last resort, not the first choice. Before deciding to deploy the Interpreter pattern, ask yourself one question: **does this DSL truly have to be home-built? Could a library plus a few data structures do the job?** Most of the time, the answer is yes.

**Second, once the grammar grows complex, the maintenance cost of a hand-written parser spikes.** Our calculator only has `+ - * / ()`, and three grammar layers tell the whole story. Once you add variables, function calls, strings, types, and error recovery, the recursive-descent code volume swells fast, while error messages get ever harder to write accurately. At that scale, the proper move is a parser generator (ANTLR, Bison) or more industrial techniques like Pratt parsing / parser combinators — not doubling down inside the GoF Interpreter pattern's framework.

**Third, the Interpreter pattern has a performance ceiling of its own.** The classic virtual-function + heap-allocated AST walks virtual dispatch and pointer hops on every evaluation — unfriendly to hot paths. The `variant` scheme above relieves this, but if you've genuinely reached JIT territory, it's time to switch technology stacks.

So when is the Interpreter pattern the **right** choice? When you have a **small DSL with well-defined grammar rules, simple structure, little risk of ballooning, and low evaluation frequency**, and off-the-shelf libraries don't directly cover it — say, an internal rule-filtering expression, a simple expression evaluation inside a config file, or a memory-frugal command parser on an embedded device. In scenarios like that, the Interpreter pattern's clear structure — "each grammar part minds its own slice, unified interface, recursive evaluation" — is actually a better deal than pulling in a heavyweight library.

## Summary

Let's run through the entire evolutionary path:

| Stage | Approach | Why It Falls Short |
|---|---|---|
| Call the library directly | `std::stoi` / `std::from_chars` | Parses a single value only; can't express "the structure of a language" |
| Turn the terminal into a class | `Number : Expression`, `interpret(Context&)` | No operators yet, nothing to compose |
| AST + evaluation | `NumberNode` / `BinaryNode` + `evaluate()` | **Good enough** (clear structure, extensible) |
| Recursive descent parser | `Parser` builds the AST from text | Hand-written maintenance cost spikes on complex grammars |
| `variant` + `visit` | Nodes flattened into a `vector`, compile-time dispatch | Readability drops; only worth it on hot paths |

Note down these key conclusions:

- **The Interpreter pattern's lifeblood is the "unified interface"** — every grammar part (terminal or non-terminal) implements the same `interpret` / `evaluate`, and the caller faces only the interface; this is a direct application of the Composite pattern's idea.
- **Precedence in recursive descent comes through naturally via the function-call hierarchy** (`expression` calls `term` calls `factor`), and left associativity comes from loops, not naive right recursion — this isn't a "detail," it's part of the grammar.
- **`std::unique_ptr` is the most natural ownership for an AST**: a parent exclusively owns its children, and recursive destruction reclaims everything automatically; consider flattening with `std::variant` only for hot paths.
- **Most "I need to parse text" needs already have ready-made wheels** — before deploying the Interpreter pattern, confirm that the DSL truly must be home-built, that the grammar is simple enough, and that the evaluation frequency is low enough.
- The Interpreter pattern often pairs with **Composite, Visitor, and Strategy**: the AST is an instance of Composite, multiple operations ride on Visitor, and swappable semantics ride on Strategy.

::: tip Companion compilable project
The examples in this section have a complete compilable project under `code/volumn_codes/vol4/design-patterns/Interpreter/` in the repo (`.h` + main + `CMakeLists.txt`); `cmake -S . -B build && cmake --build build` reproduces the outputs above.
:::

## References

- [cppreference: `std::from_chars`](https://en.cppreference.com/w/cpp/utility/from_chars) (C++17, string-to-number parsing, past-the-end iterator semantics)
- [cppreference: `std::variant` and `std::visit`](https://en.cppreference.com/w/cpp/utility/variant/visit) (C++17, compile-time type dispatch)
- [cppreference: `std::unique_ptr`](https://en.cppreference.com/w/cpp/memory/unique_ptr) (exclusive ownership for AST nodes)
- GoF, *Design Patterns: Elements of Reusable Object-Oriented Software*, Chapter 5, Interpreter (terminal / non-terminal expressions, the `interpret` interface)
- Robert Nystrom, *Crafting Interpreters*, Chapters 6–8 (recursive descent parsing, an engineering-minded walkthrough of ASTs)
