---
title: "命令模式:把「动作」变成能撤销的对象"
description: "从最直觉的「直接调函数」开始,一步步逼出 Command 接口,顺手做出一个能撤销/重做的文本编辑器,最后用 std::move_only_function 收尾"
chapter: 11
order: 13
tags:
  - host
  - cpp-modern
  - intermediate
  - 命令模式
difficulty: intermediate
platform: host
cpp_standard: [11, 17, 20, 23]
reading_time_minutes: 20
related:
  - "单例模式:从注释约束到 Meyer's Singleton"
prerequisites:
  - "Chapter 6: 类与对象"
---

# 命令模式:把「动作」变成能撤销的对象

## 我们到底在解决什么问题

我们先不上定义。想一个最常见的场景:你在写一个文本编辑器,用户点了「插入一行」,你顺手写一句 `editor.append("hello")` 就完事了——动作当场发生、当场消失。这听起来没问题,直到产品同学第二天找到你,说我们也要支持 Ctrl+Z 撤销、还要有操作历史、还要能把一组操作打包成「宏」一键重放。你回去一看代码,满屏幕都是裸函数调用,根本没有任何「发生过什么」的痕迹留下来——撤销?撤销个鬼,函数都返回了,你拿什么回滚?

命令模式要解决的就是这类需求:**把「要做的事」从一个一闪即逝的函数调用,变成一个有身份、有状态、能被存储和搬运的对象**。一旦动作成了对象,你就可以把它压进队列延迟执行、塞进日志稍后回放、组合成宏、或者最常用的——记住它,等用户按下 Ctrl+Z 时把它反向操作一遍。

但「把动作封装成对象」这八个字,在 C++ 里并不是「写个类包一层」那么简单。这里有一个非常容易踩的坑:很多人第一次写命令模式,会顺手把 `execute()` 声明成 `const`,觉得「执行命令嘛,命令对象本身又没变」,然后跑到 undo 的时候傻眼了——你根本没地方记下执行前的状态,因为你在 `const` 里承诺了不改成员。所以这一篇我们真正要回答的问题是——**怎么把一个动作干净地封装成对象,并且让它既能执行、又能反向撤销,同时不污染接收者、不依赖脆弱的运行期类型识别**。

接下来我们就一步步来,从最直觉的写法开始,看看每一步为什么不够,最后逼出一个现代 C++ 的标准答案。

## 第一步:最直觉的写法——直接调函数(反面教材)

很多人第一次遇到「编辑器要支持插入和删除」,下意识写出来的代码是这样的:

```cpp
class TextEditor {
public:
    void append_text(const std::string& line) { lines_.push_back(line); }
    void pop_text_once() {
        if (!lines_.empty()) lines_.pop_back();
    }
    void dump() const;
private:
    std::vector<std::string> lines_;
};
```

用起来也很顺手,哪里需要哪里调:

```cpp
TextEditor editor;
editor.append_text("Hello, World");
editor.pop_text_once();
```

说实话,这种写法在「动作当场发生、不需要回头」的场景里一点毛病没有,你别一看到模式就反射性地往上套。问题出在**需求开始「回头」的那一刻**——当产品同学说要撤销,你才发现 `append_text` 调完就结束了,谁也不记得当初插了什么、插了多长。撤销需要「记忆」,而裸函数调用天生没有记忆。

那么,把「记忆」加上不就行了?我们可以把「要做的动作」连同它「撤销时要做的事」一起打包,变成一个对象——这就是命令模式的雏形。

## 第二步:把动作封装成对象——抽象 Command

我们先定义一个统一的接口,所有「要做的事」都满足它。最关键的一步是同时声明 `execute()` 和 `undo()`,从一开始就让「可撤销」成为命令的内置能力,而不是事后补丁:

```cpp
struct Command {
    virtual ~Command() = default;
    virtual void execute() = 0;
    virtual void undo() = 0;   // 从第一天起就要想好怎么撤销
};
```

这里有个细节值得停下来想一想:为什么 `execute()` 不是 `const`?因为绝大多数可撤销的命令,在执行的那一刻会顺手把「撤销所需的信息」(比如插入的长度、被替换的旧值)记到自己的成员里,留给稍后的 `undo()` 用。你要是把它声明成 `const`,就等于亲手堵死了自己记状态的路——后面真要写 `undo()` 的时候,你会被 `const` 绑住,只能去搞 `mutable` 这类 hack。**所以这里的规矩是:`execute()` 不要 `const`,命令对象本身是带着状态的,它不是纯函数。**

接下来我们把「插入一段文本」做成一个具体命令。它需要持有接收者(`TextEditor`)的引用,以及它自己的参数(要插入的文本),`undo()` 则把插入的内容等长地砍掉:

```cpp
class AppendCommand : public Command {
public:
    AppendCommand(TextEditor& editor, std::string text)
        : editor_(editor), text_(std::move(text)) {}

    void execute() override { editor_.append_text(text_); }
    void undo() override    { editor_.erase_tail(text_.size()); }

private:
    TextEditor& editor_;   // 接收者:真正干活的家伙
    std::string text_;     // 参数:这个命令要插的文本
};
```

你会发现,命令对象就是三样东西的打包——一个**接收者**引用、执行所需的**参数**、以及一对 `execute()`/`undo()` 方法。接收者(`TextEditor`)对外暴露的接口(`append_text` / `erase_tail`)是稳定的,我们完全不需要动 `TextEditor` 的任何代码,就能在它头上套一层「可撤销」的能力。这就是命令模式的核心红利:**动作的「可撤销性」不再污染接收者,接收者只管「能做什么」,「能不能撤销」是命令层的事**。

## 这里先验证一下:撤销队列真能原路退回吗

口说无凭,我们写个最小的撤销栈,让它执行几条命令再一条条 undo 回去,看看缓冲区是不是真的能回到原点。先给 `TextEditor` 配一个能按长度砍尾的接口(为了支撑 undo):

```cpp
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

class TextBuffer {
public:
    void append(const std::string& s) { buf_ += s; }
    void erase_tail(std::size_t n) {
        if (n > buf_.size()) throw std::out_of_range("erase_tail");
        buf_.erase(buf_.size() - n, n);
    }
    const std::string& str() const { return buf_; }
private:
    std::string buf_;
};
```

撤销栈的逻辑简单到令人发指:`execute` 时先执行再压栈,`undo` 时弹栈顶并调用它的 `undo()`。LIFO 的特性天然契合「撤销最近一步」:

```cpp
class UndoStack {
public:
    void execute(std::unique_ptr<Command> c) {
        c->execute();
        history_.push_back(std::move(c));   // 执行成功才入栈
    }
    void undo() {
        if (history_.empty()) return;
        history_.back()->undo();
        history_.pop_back();
    }
private:
    std::vector<std::unique_ptr<Command>> history_;
};
```

跑一下,先插两段、再依次撤销,最后顺手测一下宏命令:

```sh
$ g++ -std=c++23 -O2 command_verify.cpp -o command_verify
$ ./command_verify
after 2 appends : 'Hello, World'
after 1 undo    : 'Hello, '
after 2 undo    : ''
after macro ABC : 'ABC'
after macro undo: ''
```

两次 append 之后缓冲是 `Hello, World`,撤销一次退回成只剩前半段 `Hello,`(那个尾随逗号空格还留着),再撤销一次退到空串——状态完整地原路返回了,这就是命令模式承诺的可撤销性,实打实地兑现了。

## 第三步:把多个动作打包——宏命令

前面那个 `macro ABC` 是怎么来的?答案是把命令组合起来。命令模式天然适配组合模式(composite):既然「一个动作」是命令,那「一连串动作」为什么不能也是一个命令?我们写一个 `MacroCommand`,它内部持有一组子命令,自己也是 `Command`:

```cpp
class MacroCommand : public Command {
public:
    void add(std::unique_ptr<Command> c) { subs_.push_back(std::move(c)); }

    void execute() override {
        for (auto& c : subs_) c->execute();        // 正序执行
    }
    void undo() override {
        for (auto it = subs_.rbegin(); it != subs_.rend(); ++it)
            (*it)->undo();                          // 逆序撤销
    }
private:
    std::vector<std::unique_ptr<Command>> subs_;
};
```

这里有一个真正的坑,新手特别容易栽。`execute()` 是正序遍历,`undo()` 凭什么是**逆序**?你想想栈的后进先出性质就明白了:最后执行的子命令改动了最新的状态,要回退到执行前的样子,必须先把它撤销掉,才能轮到前一个子命令。拿 `ABC` 来说,执行顺序是 `A→B→C`,撤销顺序就必须是 `C→B→A`;要是你手滑写成正序 undo,缓冲里压根没那么多东西可砍,`erase_tail` 直接越界,或者状态对不上,你会得到一个看起来还能跑、但状态已经悄悄错乱的程序。

::: warning 真正的坑:undo 必须逆序
宏命令的 `undo()` 一定要**逆序**遍历子命令,和 `execute()` 的正序严格对应。直觉上「既然正向 ABC、反向也 ABC 嘛」是错的——正向是依次累加状态,反向必须按入栈顺序逐层剥回去。写成正序 undo 是这类代码最常见的隐蔽 bug,而且往往不会立刻崩,而是在某些特定操作序列下才暴露状态错误。
:::

宏命令把一组动作打包成一个原子单位,对一个撤销栈来说,宏就是「一步」——压一次栈、undo 一次,内部的三条子命令一起回退。这正是「事务化操作」最朴素的实现方式。

## 踩坑预警:别用 dynamic_cast 取参数

讲到这里,有个常见的实现风格我得专门提一下,因为它就出现在配套工程里,坑了不少人。有些实现会反过来设计:让抽象命令带一个「类型」标签,执行时由接收者根据标签去判断「这是不是个 Append 命令」,然后用 `dynamic_cast` 把命令指针 down-cast 到派生类,好取出里面的参数:

```cpp
struct TextEditorCommand {
    enum class Type { APPEND, REMOVE };
    // ...
private:
    const Type type;
};

struct TextEditor {
    void process(Invoker* invoker) {
        for (auto& command : invoker->commands) {
            if (command->get_type() == Type::REMOVE) {
                pop_text_once();
            } else {
                // 用 dynamic_cast 取回 AppendCommand 里的 text
                AddCommand* adder = dynamic_cast<AddCommand*>(command.get());
                if (adder) append_text(adder->append);
            }
        }
    }
};
```

这种写法能跑,配套工程也是这么写的,但它是**反模式的味道很重**。问题在于,命令模式好不容易把「执行逻辑」封装进了每个命令自己的 `execute()` 里,接收者根本不用知道命令具体是哪一种;结果你一用 `dynamic_cast`,就把执行逻辑又泄漏回了接收者,接收者重新背起了「认识所有命令类型」的负担,每加一种命令都要回来改这个 switch——这恰恰是命令模式要避免的耦合。

更现实的问题是,`dynamic_cast` 依赖 RTTI(运行期类型信息),而 RTTI 在嵌入式和游戏引擎里是常常被 `-fno-rtti` 关掉的,为了省那点 `.rodata` 和 binary 体积。我们验证一下关掉 RTTI 会发生什么:

```sh
$ g++ -std=c++23 -O2 -fno-rtti command_cast.cpp
command_cast.cpp:31:20: error: 'dynamic_cast' not permitted with '-fno-rtti'
   31 |         auto* ap = dynamic_cast<AppendCmd*>(c.get());
command_cast.cpp:33:30: error: cannot use 'typeid' with '-fno-rtti'
   33 |                      typeid(*c).name(),
```

直接编译失败。也就是说,你用了 `dynamic_cast`,你的代码就再也没法进那些关 RTTI 的工程,可移植性直接打了对折。

::: warning 别让接收者认命令
正确的做法是让每个命令自己在 `execute()` 里干活,接收者只看到抽象接口 `Command&`,根本不需要知道它到底是 `AppendCommand` 还是 `EraseCommand`。只要接收者的底层接口(`append_text` / `erase_tail`)是稳定的,加新命令就是新增一个派生类,接收者一行都不用改。`dynamic_cast` + 类型标签这套写法,本质上是把命令模式退化回了 switch-case,别这么写。
:::

## 第四步:函数式命令——闭包就是命令

命令对象说到底就是一个「执行」闭包加一个「撤销」闭包。C++ 有 lambda,有 `std::function`,那我们能不能跳过手写一堆派生类,直接用两个闭包把命令糊出来?能,而且写起来非常爽。

这里直接上 C++23 的 `std::move_only_function`。你可能要问:为什么不用老的 `std::function`?因为命令对象往往需要独占资源(比如捕获一个 `unique_ptr`、一个文件句柄),而 `std::function` 要求被包装的目标是可拷贝的——它内部要拷贝闭包,move-only 的闭包它根本装不下。`std::move_only_function`(C++23 起,头文件 `<functional>`)就是为这个生的:它只要求可移动,正好匹配「一个命令对象只被执行一次、撤销一次」的独占语义。

我们把撤销栈改成接收「执行闭包 + 撤销闭包」两个参数,把这对闭包作为一个 entry 存起来:

```cpp
#include <functional>
#include <utility>
#include <vector>

class FunctionalUndoStack {
public:
    void execute(std::move_only_function<void()> do_it,
                 std::move_only_function<void()> undo_it) {
        do_it();                                   // 先执行
        history_.push_back({std::move(do_it), std::move(undo_it)});
    }
    void undo() {
        if (history_.empty()) return;
        history_.back().undo_();
        history_.pop_back();
    }
private:
    struct Entry {
        std::move_only_function<void()> do_;
        std::move_only_function<void()> undo_;
    };
    std::vector<Entry> history_;
};
```

用起来就是这样,动作和它的逆操作一起打包传进去,lambda 负责捕获一切、执行一切,你一个派生类都不用写:

```cpp
TextBuffer buf;
FunctionalUndoStack stack;
std::string chunk = "World";

stack.execute(
    [&buf, chunk] { buf.append(chunk); },          // 执行:插入
    [&buf, chunk] { buf.erase_tail(chunk.size()); } // 撤销:砍掉等长尾部
);
```

我们同样验证一下,确认闭包这条路完全跑得通:

```sh
$ g++ -std=c++23 -O2 command_lambda.cpp -o command_lambda
$ ./command_lambda
after execute: 'World'
after undo   : ''
```

干净利落。这种函数式写法的代价是什么?它的代价在于**编译期看不到类型边界**:OOP 写法里,`AppendCommand` 是一个有名字的类型,你 grep 一下就知道项目里有哪些命令、各自的 `undo()` 长什么样;函数式写法里,命令散落在各处的 lambda 里,类型系统对它们一视同仁,可读性和可发现性会差一截。所以两条路的取舍很清楚——命令种类有限、希望类型清晰、想统一加宏和日志,走 OOP 派生类;动作是一次性的、闭包能就地写死、不想为了一个动作定义一个类,走函数式闭包。两种风格不冲突,同一个项目里完全可以并存。

## 实战:一个能「优化执行」的命令队列

前面讲的撤销队列是命令模式最经典的应用,但命令模式的能力不止于此。配套工程里有一个很有意思的玩法:它不是「来一个命令执行一个」,而是先把一批命令攒在 `Invoker` 里,在真正执行前做一道**简化(simplify)**——如果一条「插入」紧跟着一条「删除」,这俩就抵消了,根本不用执行。这相当于编译器里的死代码消除,只不过发生在命令层。

我们先看简化逻辑本身,它就是一次遍历,维护一个结果栈:遇到「插入」就压进去,遇到「删除」就把栈顶的「插入」弹掉(抵消),其它情况照搬:

```cpp
void simplify() {
    std::vector<std::shared_ptr<TextEditorCommand>> result;
    for (const auto& cmd : commands) {
        if (!result.empty()
            && result.back()->get_type() == Type::APPEND
            && cmd->get_type() == Type::REMOVE) {
            result.pop_back();        // 插入 + 删除 = 啥也没干,抵消
        } else {
            result.push_back(cmd);
        }
    }
    commands = std::move(result);
}
```

我们直接拿配套工程的输入跑一遍:`ADD("Hello, World")`、`ADD("Hello, World")`、`ERASE`、`ADD("Hello, World")` 四条命令。简化器的眼睛盯着队列:前两条 ADD 各自压入结果,第三条 ERASE 一来,发现栈顶正好是 ADD,抵消掉栈顶(第二条 ADD 没了),第四条 ADD 再压入。最终队列里剩下两条 ADD,执行后缓冲区是两行 `Hello, World`:

```sh
$ g++ -std=c++23 -O2 TextEditorMain.cpp -o TextEditor
$ ./TextEditor
Hello, World
Hello, World
```

两行,正是简化后的结果。这就是命令模式比裸函数调用强的地方——因为动作是对象,所以你能在执行**之前**对它们做静态分析、做优化、做批处理、做回放,而不用动接收者一行代码。配套工程还顺手演示了 `Invoker` 的另一个能力:`append_command` 往里加命令,`remove_command` 能把指定命令从队列里拿掉——命令是对象,可以被增删、被引用、被取消,这在裸函数调用的世界里是做不到的。

::: tip 配套可编译工程
这一节的完整代码(含命令队列、简化抵消、`dynamic_cast` 取参数的反面教材)在本仓库里,自己 clone 下来 cmake 一把就能跑:[Command/TextEditor](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/code/volumn_codes/vol4/design-patterns/Command/TextEditor)。
:::

## 命令模式什么时候不该用

到这里我们已经有了一个能撤销、能组合、能优化的命令系统。但事情到这里还没完——我得诚实地告诉你,**命令模式不是万能的,很多场景下你根本不需要它,硬套反而会让代码更绕**。

第一,如果你的动作**永远不会被撤销、不会被延迟、不会被排队**,那就别套命令模式。一个 `editor.append_text("x")` 就能解决的事,你非要包一层 `AppendCommand`、塞进一个队列、再找个 Invoker 去触发它,除了多一跳间接调用、多一次堆分配,什么也没赚到。模式是为变化的需求准备的,需求没有变化时,直接调函数永远是最优解。

第二,撤销的实现成本很容易被低估。我们前面那种「插入多长就砍多长」的 undo,只对最简单的追加操作成立;一旦你的操作涉及替换、涉及光标移动、涉及多个缓冲区的联动,`undo()` 要保存的「执行前状态」会迅速膨胀,这时候你往往要借助备忘录模式(memento)把整个接收者状态快照下来,内存开销和复杂度都会上一个台阶。命令模式承诺的「可撤销」不是免费的,状态保存的代价是它最实在的成本。

第三,命令对象的**生命周期管理**是个隐形坑。命令持有接收者的引用,接收者得比命令活得长;一旦接收者先于命令析构,`execute()` / `undo()` 里的引用就成了悬空引用,这是经典的 use-after-free。在配套工程里你能看到,它用 `std::shared_ptr<TextEditorCommand>` 来管理命令本身,但接收者 `TextEditor` 的生命周期是手动保证的——一旦队列里攒了一堆命令、接收者却提前销毁,整条队列就全废了。这条在异步、跨线程场景里尤其要命。

## 小结

我们把整条演进路径捋一遍:

| 阶段 | 做法 | 为什么还不够 |
|---|---|---|
| 直接调函数 | `editor.append_text(...)` | 没有记忆,无法撤销/排队/回放 |
| 抽象 Command | `execute()` + `undo()` 派生类 | 接收者干净了,但每加一种动作要写一个类 |
| 宏命令 | 组合多个命令,逆序 undo | 解决了,但要当心 undo 的逆序坑 |
| `dynamic_cast` 取参数 | 接收者按标签 down-cast 命令 | **反模式**,泄漏耦合、依赖 RTTI、关 RTTI 直接编不过 |
| 函数式闭包 | `std::move_only_function` 装两个闭包 | 解决了,但类型边界变弱、可发现性下降 |

记下这几条关键结论:

- 命令模式的本质,是把「动作」从一闪即逝的函数调用,提升为**有身份、有状态、可存储的对象**,于是撤销、排队、回放、组合都成为可能。
- `execute()` 不要标 `const`——命令对象要记撤销所需的状态,它不是纯函数。
- 宏命令的 `undo()` 必须逆序遍历子命令,这是这类代码最常见的隐蔽 bug。
- 别用 `dynamic_cast` + 类型标签去「认」命令类型,那会把命令模式退化回 switch-case,而且关掉 RTTI 就编不过;正确做法是让命令自己在 `execute()` 里干活。
- C++23 的 `std::move_only_function` 让「两个闭包打包成一个命令」变得自然,适合一次性、move-only 的动作;但代价是类型边界变弱,要不要用取决于你对可发现性的要求。
- 命令持有接收者引用,务必保证接收者比命令活得长,否则撤销时就是 use-after-free。

## 参考资源

- [cppreference:`std::move_only_function`](https://en.cppreference.com/w/cpp/utility/functional/move_only_function)(C++23,move-only 可调用对象包装器)
- [cppreference:`std::function`](https://en.cppreference.com/w/cpp/utility/functional/function)(对比参考,要求可拷贝)
- [cppreference:`dynamic_cast`](https://en.cppreference.com/w/cpp/language/dynamic_cast)(运行期类型识别,依赖 RTTI)
- Gamma、Helm、Johnson、Vlissides,《Design Patterns》Command 一章;Klaus Iglberger,《C++ Software Design》中 Command 与类型擦除的讨论
- 配套可编译工程:[Command/TextEditor](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/code/volumn_codes/vol4/design-patterns/Command/TextEditor)
