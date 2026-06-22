---
title: "备忘录模式:把「状态」封进谁也读不懂的黑盒"
description: "从最直觉的「整盘拷贝」开始,一步步逼出快照/撤销/重做,顺手用 friend 把备忘录收紧成黑盒,再拆穿 make_shared 撞私构造的坑"
chapter: 11
order: 15
tags:
  - host
  - cpp-modern
  - intermediate
  - 备忘录模式
difficulty: intermediate
platform: host
cpp_standard: [11, 17, 20]
reading_time_minutes: 20
related:
  - "命令模式:把「动作」变成能撤销的对象"
prerequisites:
  - "Chapter 6: 类与对象"
---

# 备忘录模式:把「状态」封进谁也读不懂的黑盒

## 我们到底在解决什么问题

我们先不上定义。想一个你天天都在用、但几乎没意识到的功能:文本编辑器里的 Ctrl+Z。你打了一行字、又打了一行字、移动了几次光标,然后按下撤销——编辑器像是穿越了时间,回到几步之前的样子。你有没有想过,**它到底是怎么知道「几步之前」长什么样,而又没把你文档的内部细节(缓冲区指针、光标偏移、选区起止)满世界乱塞的?**

最暴力的办法,是在你每次敲一个键之前,把整份文档 `deep copy` 一份存起来。撤销就是回到上一份拷贝。这思路没毛病,能跑,但代价太明显:文档一大、操作一多,历史栈很快就吃光内存,而且 `deep copy` 本身可能很贵(嵌套结构、句柄、子组件一个都不能漏)。更隐蔽的麻烦是,**这份拷贝一旦到了别人手里,谁能保证它不被悄悄改掉?** 撤销系统手里握着一份"过去的你",如果它随手 `m->content = "hacked"`,你的"不可变历史"就形同虚设了。

备忘录模式要解决的就是这一类需求:**在不暴露对象内部实现的前提下,把对象的内部状态捕获下来,留到以后原样恢复**。它跟命令模式([我们前面写过的那篇](./13-command.md))是天然的一对——命令模式把"动作"对象化,备忘录模式把"状态"对象化。命令模式做 undo 靠的是"存反向操作",轻量但容易在复杂操作上算不准;备忘录模式做 undo 靠的是"存完整快照",稳妥但吃内存。什么时候用哪个,本质上是在**「反向操作的计算成本」**和**「整盘快照的存储成本」**之间做权衡。

接下来我们就一步步来,从最直觉的整盘拷贝开始,看看每一步为什么不够,最后逼出一个既封装得住、又能优雅支撑撤销重做的现代写法。

## 第一步:最直觉的写法——公开字段 + 整盘拷贝

很多朋友第一次写快照,下手大概是这样的:定义一个跟编辑器状态一模一样的结构体,字段全公开,存的时候就 `make_shared` 一份塞进去:

```cpp
struct EditorMemento {
    std::string content;
    std::size_t cursor_pos;
    EditorMemento(std::string c, std::size_t p)
        : content(std::move(c)), cursor_pos(p) {}
};

class TextEditor {
public:
    void insert(const std::string& s) {
        content_.insert(cursor_pos_, s);
        cursor_pos_ += s.size();
    }

    std::shared_ptr<EditorMemento> create_memento() const {
        return std::make_shared<EditorMemento>(content_, cursor_pos_);
    }

    void restore(std::shared_ptr<const EditorMemento> m) {
        if (!m) return;
        content_ = m->content;
        cursor_pos_ = m->cursor_pos;
    }

private:
    std::string content_;
    std::size_t cursor_pos_ = 0;
};
```

跑起来确实能用——`create_memento()` 拍一张快照,`restore()` 把它贴回去。但你仔细看一眼 `EditorMemento` 就会发现一个让人坐立不安的事实:**它的 `content` 和 `cursor_pos` 全是 public**。这意味着任何拿到这个 `shared_ptr` 的人——撤销栈、序列化模块、甚至是某个跨模块传过来的中间层——都能直接读写里面的字段。

你可能觉得,大家都是同事,谁会去改一个快照呢?但备忘录模式对封装的要求恰恰是反过来的:**它要保证"不可能被改",而不是"希望大家别改"**。一个 public 的备忘录,等于把编辑器的内部表示向整个程序摊开了。哪天有人为了调试,在撤销栈里写了个 `m->content.clear()`,你排查到崩溃都想不到是历史快照被改了。

::: warning 这里有个常被忽略的封装坑
`EditorMemento` 字段全 public 的写法,在网上的备忘录示例里铺天盖地,但它其实**违背了备忘录模式的核心承诺**:备忘录对外应当是不可读、不可写的黑盒,只有发起者(Originator)自己才能读写自己的状态。GoF 原版对这个模式的描述里特意区分了"宽接口"(只有发起者能用的完整访问)和"窄接口"(管理者只能拿到的不透明句柄)。我们第一步这个写法只有宽接口、没有窄接口,封装等于没做。这一步不改,后面撤销栈一旦复杂起来,迟早会有人在快照上动歪脑筋。
:::

所以这版能用,但封装没立起来。我们得想办法让备忘录"对发起者透明、对外界黑盒"。

## 第二步:把备忘录做成黑盒——嵌套类 + friend

C++ 给了一个几乎是为备忘录模式量身定做的机制:**把备忘录声明成发起者的嵌套类,它的构造函数和字段全部私有,然后反过来让发起者做它的 friend**。这样一来,整个程序里**只有发起者自己**能构造备忘录、能读写它的字段;其他人(包括撤销栈)拿到的是一个连 `content` 都看不见的不透明对象。

```cpp
class TextEditor {
public:
    // 备忘录是 Originator 的嵌套类型,对外是个黑盒
    class Memento {
        friend class TextEditor;  // 只有 TextEditor 能访问下面这些
        std::string content;
        std::size_t cursor_pos = 0;

        Memento(std::string c, std::size_t p)
            : content(std::move(c)), cursor_pos(p) {}

    public:
        // 外部(含 Caretaker)只能拷贝/移动这个不透明句柄,读不到内容
        Memento() = default;
        Memento(const Memento&) = default;
        Memento(Memento&&) = default;
        Memento& operator=(const Memento&) = default;
        Memento& operator=(Memento&&) = default;
    };

    std::shared_ptr<Memento> create_memento() const {
        return std::shared_ptr<Memento>(new Memento(content_, cursor_pos_));
    }

    void restore(const std::shared_ptr<Memento>& m) {
        if (!m) return;
        content_ = m->content;       // friend 授权:这里能访问私有字段
        cursor_pos_ = m->cursor_pos;
    }

    void insert(const std::string& s) {
        content_.insert(cursor_pos_, s);
        cursor_pos_ += s.size();
    }

private:
    std::string content_;
    std::size_t cursor_pos_ = 0;
};
```

你看这版的几个关键设计。备忘录被搬进了 `TextEditor` 内部,成为 `TextEditor::Memento`,它的构造函数和两个字段全是 `private`,唯一的朋友是 `TextEditor` 自己。这意味着 `create_memento` 里 `new Memento(...)` 能调通(因为发起者是 friend),`restore` 里 `m->content` 也能读(同样因为 friend);而外部代码,哪怕手里攥着一个 `shared_ptr<Memento>`,也碰不到 `content` 一个字符。

`Memento` 的 public 部分只留了默认构造和一组拷贝/移动特殊成员函数,这是为了让撤销栈(一个 `std::vector<shared_ptr<Memento>>`)和 `shared_ptr` 能正常搬运它,但它从不向外界暴露真实的内部状态。这就是 GoF 说的"窄接口"——管理者(Caretaker)拿到的是个**不透明句柄**:它能存、能传、能丢,但就是看不见里面装了什么。

我们先验证一下这套封装是不是真的成立。

## 这里先验证一下:外部真的读不到备忘录的内容吗

口说无凭,我们故意在 `main` 里写一行"试图从外面读快照内容"的代码,看看编译器答不答应:

```cpp
int main() {
    TextEditor editor;
    editor.insert("Hello");
    auto snap = editor.create_memento();

    // 外部代码试图读取 m->content —— 这一行应当编译失败
    std::cout << snap->content << "\n";   // ERROR
    return 0;
}
```

编译,贴真实输出(`g++ 16.1.1`,`-std=c++23 -O2`):

```sh
$ g++ -std=c++23 -O2 memento_encap_break.cpp -o memento_encap_break
memento_encap_break.cpp:53:24: error: 'std::string TextEditor::Memento::content'
      is private within this context
memento_encap_break.cpp:10:21: note: declared private here
```

编译器直接把我们挡在门外:`content is private within this context`。这就是 friend + 嵌套类把封装钉死的效果——**不是靠注释提醒,也不是靠约定,是编译期硬约束**。撤销栈、序列化模块、任何外部代码,都只能拿到一个不透明的 `shared_ptr<Memento>`,对它内部字段的存在一无所知。

正常的"存快照、再恢复"流程则是通的,我们把它跑一遍:

```sh
$ g++ -std=c++23 -O2 memento_verify2.cpp -o memento_verify2
$ ./memento_verify2
Content: "Hello, world" | Cursor@12
Content: "Hello" | Cursor@5
```

第一行是恢复前(已经插入了 `, world`),第二行是 `restore(snap)` 之后——光标和内容都精确回到 `Hello` 那一刻。封装立住了,功能也没丢。

## 踩坑预警:make_shared 撞上私有构造

::: warning 踩坑预警
如果你照着第一步的惯性,在第二步里也用 `std::make_shared<Memento>(...)` 来创建快照,代码会**编不过**,而且报错信息长得吓人,新人看到容易当场放弃。我们先把坑踩给你看,再讲为什么。

把 `create_memento` 改成下面这行:

```cpp
std::shared_ptr<Memento> create_memento() const {
    return std::make_shared<Memento>(content_, cursor_pos_);  // ⚠️ 编不过
}
```

编译,真实输出(截取关键行):

```sh
$ g++ -std=c++23 -O2 memento_verify.cpp -o memento_verify
.../stl_construct.h:133:7: error: 'Memento(std::string, std::size_t)'
      is private within this context
.../memento_verify.cpp:15:9: note: declared private here
```

错误的核心是 `Memento(...)` 的构造函数 `is private within this context`——也就是说,构造调用发生在**一个无权访问私有构造的上下文**里。

问题出在哪?`std::make_shared` 不是直接 `new` 完事,它要把对象和控制块分配在同一块内存里,因此内部走的是 `std::allocator_traits<...>::construct` -> `std::construct_at` -> placement `new` 这条路径。这条路径上的代码**是标准库内部的,不是 `TextEditor`**;而你声明的 `friend class TextEditor` 只放行了 `TextEditor` 这一个类,标准库的分配基础设施根本不在白名单里。于是 `construct_at` 想调用私有构造函数时,被访问控制一脚踹了回来。
:::

解决办法非常直接:**绕开 `make_shared`,改用 `std::shared_ptr<Memento>(new Memento(...))`**。这条路径是 `TextEditor` 自己在 `create_memento` 里**直接**调用私有构造(发起者本身就是 friend,有权调用),不经过任何标准库分配器基础设施,所以能编过。

```cpp
std::shared_ptr<Memento> create_memento() const {
    // TextEditor 是 Memento 的 friend,这里直接调私有构造,合法。
    // 不走 make_shared,否则 allocator_traits::construct 会撞访问控制。
    return std::shared_ptr<Memento>(new Memento(content_, cursor_pos_));
}
```

代价是少了一次控制块合并、多了一次独立堆分配(`make_shared` 会把对象和控制块塞进一次分配,`shared_ptr(new ...)` 是两次)。对备忘录这种**创建频率不高、生命周期相对短**的对象,这点开销完全可以接受,换来的是真正立得住的封装。如果你特别在意这一次分配,还有别的路子(比如给 `Memento` 配一个 `std::enable_shared_from_this` 加静态工厂,或者干脆用值语义的 `Memento` 而不是 `shared_ptr`),但都会让代码复杂一截,大多数场景下不值当。

记住这条结论:**一旦你用 friend + 私有构造来做备忘录封装,创建快照就别用 `make_shared`**,用 `shared_ptr(new ...)` 就行。

## 实战:带撤销/重做的历史栈

单张快照只能"回到某一刻"。真正常用的编辑器是支持**连续撤销、连续重做**的:我撤销了三步,又改主意重做两步,中间还可能插入新编辑把"重做未来"全部作废。这就需要一个独立的 `History` 类(也就是 GoF 里的管理者,Caretaker),它维护一条线性的快照序列和一个"当前指针",撤销就是指针后退,重做就是指针前进。

```cpp
class History {
public:
    void push(std::shared_ptr<TextEditor::Memento> m) {
        // 在非末尾处插入新快照时,丢弃之后的 redo 分支
        if (cursor_ + 1 < static_cast<int>(stack_.size())) {
            stack_.erase(stack_.begin() + cursor_ + 1, stack_.end());
        }
        stack_.push_back(std::move(m));
        cursor_ = static_cast<int>(stack_.size()) - 1;
    }

    bool can_undo() const { return cursor_ > 0; }
    bool can_redo() const {
        return cursor_ + 1 < static_cast<int>(stack_.size());
    }

    std::shared_ptr<TextEditor::Memento> undo() {
        if (!can_undo()) return nullptr;
        --cursor_;
        return stack_[static_cast<std::size_t>(cursor_)];
    }

    std::shared_ptr<TextEditor::Memento> redo() {
        if (!can_redo()) return nullptr;
        ++cursor_;
        return stack_[static_cast<std::size_t>(cursor_)];
    }

private:
    std::vector<std::shared_ptr<TextEditor::Memento>> stack_;
    int cursor_ = -1;   // -1 表示空历史
};
```

这版有几个值得说的设计点。`push` 里第一段 `if` 是"丢弃 redo 分支"的逻辑——它的意思是:**只要你从历史中间(已经撤销过几步)的位置再插入新快照,你就相当于在时间线上开了条新岔路,原来那条"重做未来"就不该再存在了**。这正是你熟悉的编辑器行为:撤销两步、再敲一个新字符,原来的重做链路就没了。这一步不改,重做栈就会跟实际状态对不上,撤销系统会给你恢复出一份"从未存在过的过去"。

`cursor_` 用 `int` 而不是 `size_t`,是为了让 `-1` 这个"空历史"状态有个自然的表示;和 `stack_.size()`(无符号)比较时一律用 `static_cast<int>` 做显式转换,避免有符号/无符号比较警告。`undo` / `redo` 都走先判 `can_undo` / `can_redo` 再动指针的路子,空历史或越界时返回 `nullptr`,调用方拿到 `nullptr` 自然就不会去 restore,语义自洽。

你还会注意到,`History` 持有的是 `std::shared_ptr<TextEditor::Memento>`——一个**必须写全限定名**的类型,因为 `Memento` 是 `TextEditor` 的嵌套类。这一点其实暴露了一个设计取舍:备忘录作为嵌套类型虽然封装好,但让管理者(Caretaker)在类型层面**依赖了发起者**。在我们这个简单场景里无所谓,但如果你想让 `History` 成为一个能服务于任意多种发起者的通用撤销框架,就得把"不透明句柄"进一步抽象成类型擦除的 `std::any` 或者一个只暴露 `apply()` 的接口——那是另一篇文章的话题了,这里先不展开。

跑一遍,看撤销重做和分支丢弃到底对不对:

```sh
$ g++ -std=c++23 -O2 memento_history.cpp -o memento_history
$ ./memento_history
Content: "Hello, world" | Cursor@12
[undo] Content: "Hello" | Cursor@5
[undo] Content: "" | Cursor@0
[redo] Content: "Hello" | Cursor@5
[edit] Content: "Hello!!!" | Cursor@8
can_redo = 0 (expect 0)
can_undo = 1 (expect 1)
```

捋一下这条轨迹。插入两段文本后状态是 `Hello, world`,撤销两次依次回到 `Hello` 和空串;重做一次又推进到 `Hello`;这时在 redo 中途插入 `!!!`,`can_redo` 立刻变回 `0`——原来的"重做未来"被干净地丢弃了,`can_undo` 仍然是 `1`,因为新的 `Hello!!!` 状态自己也是可撤销的。行为完全符合预期。

## 什么时候该用备忘录,什么时候别用

到这里我们已经有了一个封装扎实、能撤销能重做的实现。但事情到这里还没完——我得诚实地告诉你,备忘录模式不是万能药,它有自己的账要算。

**第一,它吃内存,而且是线性吃。** 每存一份快照就是一份完整的状态拷贝。对一个几百 KB 的文本文档,撤销个几十次还好;但如果你要快照的对象是一个带几百万个顶点的 3D 场景、或者一个塞满缓存的大型业务对象,历史栈很快就会把内存吃光。缓解的办法有几个:限制历史深度(只保留最近 N 步)、做差量快照(只存跟上一步相比变化的部分)、或者干脆放弃完整快照改用[命令模式](./13-command.md)只存反向操作。差量快照听起来美,实现起来却很容易出错——你得保证"任意一份增量加上基线都能精确还原",边界条件多到让人头秃,所以多数实现还是老老实实存全量、靠限制深度来兜底。

**第二,封装的成本不在写,在维护。** 发起者每加一个内部状态字段,你得记得在 `create_memento` 和 `restore` 里同步处理它。漏一个字段,撤销就会出现"为什么我撤销之后这个配置项没回去"的玄学 bug——而且这种 bug 只在用户真的去撤销那个特定字段时才暴露,测试覆盖率稍微不够就溜过去了。一个实用的纪律是:**备忘录的字段集合应当和发起者需要参与快照的状态字段集合一一对应**,新增字段时把备忘录当成发起者的"镜像"一起改。

**第三,它和命令模式不是二选一,是经常配合着用。** 命令模式擅长"把动作对象化、可重放、可打包成宏",但它的 undo 靠算反向操作,复杂操作容易算不准;备忘录模式擅长"稳妥地回到某个确定状态",但吃内存且粒度粗。真实工程里常见的组合是:**用命令模式组织操作流,用备忘录给那些"反向操作算不准"的复杂命令兜底**——命令执行前拍一张快照,撤销时就 restore,既保留了命令模式的重放和宏能力,又用快照换来了撤销的绝对正确。我们前面那篇命令模式文章里提到过,一旦操作涉及替换、光标移动、多缓冲区联动,`undo()` 要保存的"执行前状态"会迅速膨胀,那时候你往往就要借助备忘录——这两篇文章在这里接上了。

::: tip 命令模式 vs 备忘录模式,怎么选
一句话:**操作简单、状态大,用命令模式**(存反向操作,省内存);**操作复杂、状态小,用备忘录模式**(存完整快照,稳妥)。两者都复杂的时候,用命令模式搭骨架、备忘录给复杂命令兜底。
:::

## 小结

我们把整条演进路径捋一遍:

| 阶段 | 做法 | 为什么还不够 |
|---|---|---|
| 公开字段备忘录 | `struct EditorMemento { public: ... }` | 字段全 public,封装形同虚设,快照可被任意篡改 |
| 嵌套类 + friend | 私有构造、私有字段、`friend class Originator` | 封装立住了,但创建要用 `shared_ptr(new ...)`,不能用 `make_shared` |
| 历史栈 Caretaker | `vector<shared_ptr<Memento>>` + 当前指针 | 单备忘录只能"回某一刻",要连续撤销/重做得靠管理者 |
| 命令 + 备忘录混用 | 命令组织操作流,复杂命令执行前拍快照 | 纯快照吃内存,纯命令反向操作算不准,两者互补 |

记下这几条关键结论:

- **备忘录的核心是封装,不是快照本身**——快照谁都拍得出来,但只有"对外黑盒、对发起者透明"的快照,才配叫备忘录模式。在 C++ 里实现这一点,标配做法是嵌套类 + 私有构造 + `friend class Originator`。
- **创建快照用 `std::shared_ptr<Memento>(new ...)`,别用 `std::make_shared`**——私有构造在 `make_shared` 走的 `allocator_traits::construct` 路径下会触发访问控制错误,这是这一章最容易绊倒人的坑。
- **撤销/重做的本质是一条带指针的线性历史**——`undo` 是指针后退,`redo` 是指针前进,在非末尾插入新快照时丢弃之后的 redo 分支,这就是你熟悉的所有编辑器的行为模型。
- **备忘录吃内存,且字段要和发起者镜像维护**——状态大、操作简单时优先考虑命令模式的反向操作;两者复杂度都高时,命令搭骨架、备忘录给复杂命令兜底。

::: tip 配套可编译工程
本节的例子在仓库 `code/volumn_codes/vol4/design-patterns/Memento/` 下有完整可编译工程(`.h` + main + `CMakeLists.txt`),`cmake -S . -B build && cmake --build build` 即可跑出上面这些输出。
:::

## 参考资源

- [cppreference:`std::shared_ptr` 和 `std::make_shared`](https://en.cppreference.com/w/cpp/memory/shared_ptr/make_shared)(`make_shared` 与 `shared_ptr(new ...)` 的分配差异,C++11 起)
- [cppreference:嵌套类与友元](https://en.cppreference.com/w/cpp/language/nested_type)(C++ 嵌套类与 `friend` 的访问控制语义)
- GoF,《设计模式:可复用面向对象软件的基础》—— 备忘录模式(Memento)原始定义,提出"宽接口 / 窄接口"二分
