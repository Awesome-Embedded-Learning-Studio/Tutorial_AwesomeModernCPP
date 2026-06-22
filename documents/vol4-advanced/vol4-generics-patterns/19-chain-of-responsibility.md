---
title: "责任链模式:从一长串 if/else 到 next_ 指针,再到中间件洋葱"
description: "从最直觉的「调用方写死该谁处理」起步,一步步逼出经典的 next_ 指针链,看清它解决了什么、又把耦合搬到了哪里,最后用 std::vector 调度器和 std::function 中间件洋葱给出两种现代 C++ 的替代写法"
chapter: 11
order: 19
tags:
  - host
  - cpp-modern
  - intermediate
  - 责任链模式
difficulty: intermediate
platform: host
cpp_standard: [11, 17, 20]
reading_time_minutes: 20
related:
  - "单例模式:从注释约束到 Meyer's Singleton"
  - "策略模式:从一堆 if/else 到编译期可替换的 Policy"
prerequisites:
  - "Chapter 6: 类与对象"
---

# 责任链模式:从一长串 if/else 到 next_ 指针,再到中间件洋葱

## 我们到底在解决什么问题

我们先不急着上定义。想一个特别具体的场景:你写了一个网络服务,现在要处理进来的请求。一个请求进来,你想按顺序过这么几道关——先看它有没有合法的认证 token,没认证就直接拒掉;认证过了,记一条访问日志;日志记完,再交给真正的业务逻辑去算结果。这些处理步骤,顺序是固定的,而且「该不该继续往下走」是每一步自己说了算的(认证失败就到此为止,后面那几步根本不该跑)。

最直觉的写法,是调用方自己把这个顺序写死:

```cpp
void handle_request(const Request& req) {
    if (!check_auth(req)) {
        reject(req);
        return;
    }
    log_access(req);
    if (!run_business(req)) {
        respond_error(req);
        return;
    }
}
```

刚开始这么写没什么问题。可接下来事情就开始失控了。产品说要加一个限流(每个 IP 每分钟最多 100 次),你把限流插在认证之前。又过两天说要加一个指标埋点(每一步耗时统计),你又插一层。再后来要做灰度(只有某些用户走新逻辑),又一层。现在你打开 `handle_request`,里面是一长串按顺序排好的步骤,每一步都「可能拦截、可能放行」,顺序稍错一点就是线上事故,而每加一层新逻辑,你都要**打开这个函数、改它的内部结构**。

这件事的根子在于:**「请求该由谁处理、按什么顺序处理、什么时候停」这件事,被焊死在了调用方里**。调用方不仅知道有哪几道关,还知道它们的先后、知道谁能让谁停。我们想要的是反过来——**调用方只负责把请求丢进一个链条,链条上的每个节点自己决定「我处理,还是甩给下一个」**,链条怎么拼、加几层、什么顺序,调用方一概不知。

责任链模式(Chain of Responsibility)要解决的就是这件事。GoF 给的意图一句话:**「让多个对象都有机会处理一个请求,把这些对象串成链,请求沿链传递,直到某个对象把它处理掉」**——目的就是让请求的**发送方**和**接收方**解耦,发送方不知道、也不需要知道最终是谁处理的。

但「串成链」这个动作在 C++ 里有好几种实现,每种都各有各的坑。我们一步步来,从最蠢的写法开始,看每一种是怎么被前一种的痛点逼出来的。

## 第一步:最原始的写法——调用方写死顺序(反面教材)

我们已经看过这个开头了,把它再放大一点,看看痛点到底出在哪:

```cpp
void handle_request(const Request& req) {
    if (!rate_limit(req)) { reject_429(req); return; }
    if (!check_auth(req)) { reject_401(req); return; }
    log_access(req);
    if (!run_business(req)) { respond_error(req); return; }
    respond_ok(req);
}
```

能跑,但毛病一大堆。**第一**,每加一道新关卡,你就要回头改这个函数——它是「对扩展封闭」的,违背开闭原则。**第二**,关卡之间的顺序是隐式的(从上往下读才知道限流在认证之前),没有一个地方能让你一眼看清「这条链长什么样」。**第三**,也是最隐蔽的一点——这些关卡是写死的函数调用,你想在运行时根据配置动态拼一条链(比如「内部测试环境不要限流」),根本做不到,除非又加一堆 `if`。

这条路走不通的根子在于:**「有哪些处理者」和「调用方怎么用这些处理者」被揉在了同一个函数里**。我们真正想要的是,把「处理者集合」和「它们怎么被串起来」这两件事从调用方里抽出去,变成一个可以独立组装的结构。每个处理者只回答两个问题:**「这个请求我能处理吗?」「不能的话,把它交给下一个。」**

这正是经典的 next_ 指针链要做的事。

## 第二步:经典指针链——`next_` 指针 + 处理或转发

我们直接上代码,然后逐行拆它为什么这么写。这是 GoF 经典的指针链实现:一个抽象的 `Handler`,每个具体处理器继承它,手里捏着一个指向下一个处理器的 `next_` 指针:

```cpp
#include <iostream>
#include <memory>
#include <string>

class Handler {
public:
    virtual ~Handler() = default;

    void set_next(std::shared_ptr<Handler> next) {
        next_ = std::move(next);
    }

    // 模板方法:把"转发"逻辑焊死在基类,子类只管 process
    void handle(const std::string& req) {
        const bool handled = process(req);
        if (!handled && next_) {
            next_->handle(req);          // 甩给下一个
        } else if (!handled && !next_) {
            std::cout << "[chain end] nobody handled: " << req << "\n";
        }
    }

protected:
    virtual bool process(const std::string& req) = 0;  // 返回 true 表示"我处理了"

private:
    std::shared_ptr<Handler> next_;
};

class AuthHandler : public Handler {
protected:
    bool process(const std::string& req) override {
        if (req == "auth") {
            std::cout << "AuthHandler handled\n";
            return true;
        }
        return false;
    }
};

class LogHandler : public Handler {
protected:
    bool process(const std::string& req) override {
        if (req == "log") {
            std::cout << "LogHandler handled\n";
            return true;
        }
        return false;
    }
};
```

用起来是这样:

```cpp
int main() {
    auto auth = std::make_shared<AuthHandler>();
    auto log = std::make_shared<LogHandler>();
    auth->set_next(log);

    auth->handle("log");   // auth 拒收 -> 转给 log -> log 处理
    auth->handle("auth");  // auth 自己处理
    auth->handle("xxx");   // 一路没人收,到链尾报"nobody handled"
}
```

编译跑一下(GCC 16.1.1):

```sh
$ g++ -std=c++23 -O2 -Wall chain_verify.cpp -o chain_verify
$ ./chain_verify
=== pointer chain ===
LogHandler handled
AuthHandler handled
[chain end] nobody handled: xxx
```

你看,调用方只知道 `auth->handle(req)`,它**完全不知道**后面还有没有节点、有几个节点、是谁。这就是责任链的核心收益——发送方和接收方解耦。请求在链上流动,每个节点要么自己吃掉(返回 `true`),要么甩给 `next_`,直到有人吃掉或者链到头。

### 这套设计的精巧之处:`handle` 和 `process` 为什么分开

你可能会问:`handle` 和 `process` 干嘛要拆成两个函数?直接在子类里写一个虚函数 `handle`,里面自己决定要不要转发,不更简单吗?

能写,但那样你就把「转发到 `next_`」这件事的负担压到了**每一个子类**身上——每个具体处理器都得记得写「如果我没处理、而且有 next_,就调 next_->handle(req)」这一坨。早晚会有人写漏(忘了转发,或者转发错了),而编译器一声不吭。

经典的指针链用了一个特别漂亮的手法来根治这个问题:**把「转发」逻辑提到基类的非虚 `handle` 里焊死,子类只暴露一个纯虚 `process` 回答「这个请求我处理了吗」**。注意 `handle` 在基类里是**非虚**的——它是一个模板方法(template method),控制了「先让子类试,不行就转发」这个固定骨架,子类改不了这个骨架,只能填 `process` 这个空。这样一来,「转发」这件事只写一次,永远不会写漏。

这套写法有个名字,叫 **模板方法 + 责任链**的组合。它把**不变的部分(转发骨架)**和**可变的部分(每个节点的判断逻辑)**干净地分开了。你在写责任链时,如果发现每个节点都要手写转发逻辑,那就是这个拆分没做对。

## 这里先验证一下:转发逻辑真的只跑一次吗

口说无凭,我们把转发过程打印出来,确认「一个请求要么被某个节点吃掉、要么一路走到链尾」,中间不会重复处理。我们在每个节点的 `handle` 里加打印,看请求的流动轨迹:

```cpp
#include <iostream>
#include <memory>
#include <string>

class Handler {
public:
    virtual ~Handler() = default;
    void set_next(std::shared_ptr<Handler> next) { next_ = std::move(next); }

    void handle(const std::string& req) {
        std::cout << "  -> enter " << name() << " with '" << req << "'\n";
        if (process(req)) {
            std::cout << "  <- " << name() << " handled it, chain stops\n";
            return;
        }
        std::cout << "  <- " << name() << " passed (no match)\n";
        if (next_) next_->handle(req);
        else std::cout << "  <- chain end, nobody handled\n";
    }

protected:
    virtual bool process(const std::string& req) = 0;
    virtual const char* name() const = 0;

private:
    std::shared_ptr<Handler> next_;
};

class AuthHandler : public Handler {
protected:
    bool process(const std::string& req) override { return req == "auth"; }
    const char* name() const override { return "Auth"; }
};

class LogHandler : public Handler {
protected:
    bool process(const std::string& req) override { return req == "log"; }
    const char* name() const override { return "Log"; }
};

int main() {
    auto auth = std::make_shared<AuthHandler>();
    auto log = std::make_shared<LogHandler>();
    auth->set_next(log);

    std::cout << "[request: log]\n";   auth->handle("log");
    std::cout << "[request: auth]\n";  auth->handle("auth");
    std::cout << "[request: xxx]\n";   auth->handle("xxx");
}
```

跑一下:

```sh
$ g++ -std=c++23 -O2 -Wall chain_trace.cpp -o chain_trace
$ ./chain_trace
[request: log]
  -> enter Auth with 'log'
  <- Auth passed (no match)
  -> enter Log with 'log'
  <- Log handled it, chain stops
[request: auth]
  -> enter Auth with 'auth'
  <- Auth handled it, chain stops
[request: xxx]
  -> enter Auth with 'xxx'
  <- Auth passed (no match)
  -> enter Log with 'xxx'
  <- Log passed (no match)
  <- chain end, nobody handled
```

轨迹很清楚:请求沿链单向前进,要么停在某个节点(它 process 返回 true,链立即终止),要么一路走到链尾报「nobody handled」,**绝不会回退、不会重复进入同一个节点**。这条线性流动的性质,是经典责任链最重要的不变量——后面「中间件洋葱」和「可回溯责任链」要做的,正是打破、重新讨论这条不变量。

## 踩坑预警:next_ 指针链没你想的那么解耦

::: warning next_ 指针链把耦合挪了个地方,不是消除了
指针链确实让**调用方**和**具体处理器**解耦了——调用方不知道后面是谁。但它把耦合搬到了**节点之间**。每个节点手里捏着一个 `next_` 指针,这件事带来三个实际工程痛点。

**第一,在中间插一个节点,你得手动重连指针。** 假设你有 `auth -> log`,现在想在中间插一个 `metrics`,你不能只把 `metrics` 加进来——你必须去改 `auth` 的 `next_`,让它指向 `metrics`,再把 `metrics` 的 `next_` 指向 `log`。我们在编译器里验证一下这个重连成本:

```cpp
auto metric = std::make_shared<MetricsHandler>();
metric->set_next(log);      // metrics 接上原来的尾巴
auth->set_next(metric);     // auth 的 next 改指向 metrics
// 现在:auth -> metric -> log
```

跑出来(完整代码在配套工程):

```sh
$ ./chain_verify
=== inserting middle node into pointer chain: relink cost ===
MetricsHandler handled
LogHandler handled
```

重连成功了,但你要看到代价:**你为了插一个节点,碰了链条上一个不相关节点(auth)的内部状态**。如果 `auth` 是别人维护的、你只有个指针,这根本改不动。

**第二,链的形状散落在每个节点的 `next_` 里,没有一个地方能让你一眼看清「整条链长什么样」。** 调试时你想打印这条链,得从链头一路 `next_` 跟下去,中间断了、成环了、连错了,编译期一概查不出来,只能运行时撞。

**第三,`next_` 指针让节点持有彼此**,链上每个节点都 `shared_ptr` 着下一个,稍不留神就容易成环(`A->B, B->A`),一成环就是内存泄漏——`shared_ptr` 的引用计数永远归不了零。指针链的教材示例里这点从不提,但真到工程里,链的拼装和销毁是最容易出事的地方。
:::

这就是指针链的真实账本:**它解决了「调用方不该知道谁处理」的问题,但代价是把链的组装责任分散到了每个节点头上**。在节点少、链基本不变的简单场景,这点代价无所谓;一旦节点会动态增减、链要在运行时拼装,指针链就显得捉襟见肘。我们得换一种把链「收拢」起来的写法。

## 第三步:用 `std::vector` 收拢链——把链变成一个集合

既然 `next_` 指针的毛病是「链散落在每个节点里」,那最直接的解药就是——**把链收拢成一个集合,由一个专门的调度器统一管**。整个链的形状不再藏在节点的 `next_` 里,而是明明白白地摆在一个 `std::vector` 里。配套的可编译工程里就是这么写的,我们照它的思路来:

```cpp
#pragma once
#include <memory>
#include <print>
#include <vector>

struct Message {
    enum Type { kDisk, kConsole, kGuiScreen };
    Message(Type t, std::string msg) : type(t), text(std::move(msg)) {}
    const Type type;
    const std::string text;
};

struct Handler {
    virtual ~Handler() = default;
    virtual bool can_accept(const Message& m) = 0;
    virtual void process(const Message& m) = 0;
};

struct DiskHandler : Handler {
    bool can_accept(const Message& m) override { return m.type == Message::kDisk; }
    void process(const Message& m) override { std::println("From Disk: {}", m.text); }
};

struct ConsoleHandler : Handler {
    bool can_accept(const Message& m) override { return m.type == Message::kConsole; }
    void process(const Message& m) override { std::println("From Console: {}", m.text); }
};

struct GuiHandler : Handler {
    bool can_accept(const Message& m) override { return m.type == Message::kGuiScreen; }
    void process(const Message& m) override { std::println("From GUI: {}", m.text); }
};

struct HandlerChain {
    HandlerChain() {
        handlers_.emplace_back(std::make_shared<DiskHandler>());
        handlers_.emplace_back(std::make_shared<ConsoleHandler>());
        handlers_.emplace_back(std::make_shared<GuiHandler>());
    }

    void dispatch(const Message& m) {
        for (const auto& h : handlers_) {
            if (h->can_accept(m)) {
                h->process(m);
            }
        }
    }

private:
    std::vector<std::shared_ptr<Handler>> handlers_;
};
```

用起来:

```cpp
#include "OutputHandler.h"

int main() {
    HandlerChain chain;
    chain.dispatch({Message::kDisk, "Hello, World"});
    chain.dispatch({Message::kConsole, "Hello, World"});
    chain.dispatch({Message::kGuiScreen, "Hello, World"});
}
```

跑一下:

```sh
$ g++ -std=c++23 -O2 -Wall chain_verify.cpp -o chain_verify
$ ./chain_verify
=== vector dispatcher ===
From Disk: Hello, World
From Console: Hello, World
From GUI: Hello, World
```

这一版和指针链的差别,我们得讲清楚,因为它**不是简单的等价替换,而是语义变了**。

### 这里的关键差别:广播 vs 首中即停

仔细看 `HandlerChain::dispatch` 的循环:它**遍历所有 handler**,每个 `can_accept` 返回 true 的都调一次 `process`,**没有 break**。这意味着——如果有两个 handler 都能 accept 同一条消息,两个都会被触发。这是一种**广播(broadcast)**语义。

而经典指针链是**首中即停(first-match-wins)**:第一个 process 返回 true 的节点吃掉请求,后面的节点根本不知道这条消息来过。

这两者不是一回事,选错会出事。日志框架(一条 log 可能同时写文件和发网络)通常是广播;审批流(报销单经理批了就不用总监看了)通常是首中即停。如果你想要首中即停的语义,上面的循环得加一句 `break`:

```cpp
void dispatch_first_match(const Message& m) {
    for (const auto& h : handlers_) {
        if (h->can_accept(m)) {
            h->process(m);
            break;   // 第一个吃掉的就把请求终结,后面的不跑
        }
    }
}
```

所以记一条:**「vector 调度器」本身不规定是广播还是首中即停,它把决定权交还给了 `dispatch` 的写法**——你想要哪种,在循环里加不加 `break` 就行。这反而是 vector 版相对指针版的一个好处:指针链的「首中即停」是写死在骨架里的,你想改成广播得改基类;vector 版改语义只要改一个循环。配套的 Playground 工程用的就是广播语义(遍历所有 handler,不 break),这一点你要意识到。

### vector 版解决了什么、还差什么

vector 版把链收拢成一个集合之后,**前面那三个指针链的痛点全解决了**:插中间节点只要 `handlers_.insert(it, new_handler)`,不碰任何已有节点;整条链的形状一眼看清(就是个 vector,打印它就行);节点之间不再互持 `next_` 指针,成环泄漏的隐患也没了。

但 vector 版也引入了一个新限制:**节点的「转发」自主权被拿走了**。在指针链里,一个节点可以在 `process` 里决定「我处理一半,然后自己主动把请求往后续传」——它对转发有完全的控制权。在 vector 版里,「要不要继续往下走」是调度器(`dispatch` 循环)说了算,节点只回答「我 accept 不 accept」这一个布尔问题。对于「每一步独立判断能不能干」的简单流水线,这够了;但对于「这一步处理完了之后,我想基于处理结果决定后面走不走」这种更复杂的编排,vector 版就力不从心了。

下一节我们看一种专门为后者设计的写法——中间件洋葱。

## 第四步:中间件洋葱——节点自己决定「前、后、转不转」

前面三种写法里,节点都只回答一个问题:「这个请求我处理不处理」。但真实的中间件(你用过 Express.js、Koa、ASP.NET 的 pipeline 吧)要做的远不止这个——一个中间件想在**调用下一个之前**干点事(打计时起点)、在**下一个返回之后**再干点事(算耗时、打日志)、甚至**压根不调用下一个**(认证失败直接短路)。这种「每个节点既有前置、又有后置、还能短路」的需求,把责任链从一根单向直线,变成了一颗「洋葱」——请求一层一层穿进去,响应再一层一层穿出来。

在 C++ 里实现这套,最干净的做法是:**节点不再回答布尔问题,而是变成一个「拿到 next 就自己决定怎么用」的函数**。一个统一的调度器负责把「下一个」按顺序喂给每个节点:

```cpp
#include <functional>
#include <iostream>
#include <vector>

class MiddlewareChain {
public:
    // 每个中间件:拿到"下一个"的引用,自己决定怎么编排
    using Middleware = std::function<void(MiddlewareChain&)>;

    void use(Middleware m) { middlewares_.push_back(std::move(m)); }

    void next() {
        if (index_ < middlewares_.size()) {
            auto m = middlewares_[index_++];
            m(*this);   // 把自己(也就是"如何继续")交给中间件
        }
    }

private:
    std::vector<Middleware> middlewares_;
    std::size_t index_ = 0;
};
```

注意这里的两个设计要点。**第一,中间件的签名是 `void(MiddlewareChain&)`,它拿到的不是一个「下一个中间件」,而是整个 chain 的引用**,自己通过调 `chain.next()` 来推进——这就把「要不要往下走」的权力完全交给了中间件本身。**第二,`next()` 里有一个 `index_` 游标**,每调一次推进一格,这就避免了指针链那种「每个节点互持指针」的耦合,同时又保留了「节点自己控制转发」的自主权。

用起来:

```cpp
int main() {
    MiddlewareChain chain;
    chain.use([](MiddlewareChain& c) {
        std::cout << "before: auth\n";
        c.next();                 // 主动放行
        std::cout << "after: auth\n";
    });
    chain.use([](MiddlewareChain& c) {
        std::cout << "before: logging\n";
        c.next();
        std::cout << "after: logging\n";
    });
    chain.use([](MiddlewareChain&) {
        std::cout << "final handler\n";
        // 不调 c.next(),链自然到此为止
    });
    chain.next();
}
```

编译跑一下(GCC 16.1.1):

```sh
$ g++ -std=c++23 -O2 -Wall chain_proxy.cpp -o chain_proxy
$ ./chain_proxy
=== proxy/middleware chain ===
before: auth
before: logging
final handler (no proceed -> chain stops)
after: logging
after: auth done
```

你看这个输出的顺序——所有的 `before` 先一层层进去,到 `final handler` 拐弯,然后所有的 `after` 再一层层出来。这就是「洋葱」:请求从外向里穿,响应从里向外穿,每个中间件天然能同时做「前置」和「后置」两件事。这个能力是前面三种写法都没有的。

### 想短路?不调 `next()` 就行

洋葱模型最实用的特性是**短路**:认证失败的中间件只要不调 `c.next()`,链就停在这里,后面的中间件(业务逻辑)根本不会被触发。我们验证一下:

```cpp
chain.use([](MiddlewareChain& c) {
    std::cout << "A: 认证失败,不调 next\n";
    // 故意不调 c.next() -> 链停在这
    (void)c;
});
chain.use([](MiddlewareChain&) {
    std::cout << "B: 这一行不该出现\n";
});
chain.next();
```

跑出来:

```sh
=== middleware can SHORT-CIRCUIT by not calling proceed ===
A: 认证失败,不调 next
```

B 没出现。**短路是「什么都不做」就实现的——不调 `next()` 即可,没有 `return false`、没有 `break`,控制流干净到近乎隐式**。这是洋葱模型相对 vector 调度器最大的表达力优势:中间件可以基于自己的判断(认证过没、限流超没超)来决定整条链的生死,而这个判断逻辑完全封装在中间件内部,调度器一无所知。

::: warning 别把 index_ 游标当万能的
洋葱模型里那个 `index_` 游标有个暗坑:**它是一次性的**。一条 `MiddlewareChain` 跑过一遍之后,`index_` 已经顶到了 `middlewares_.size()`,再调 `next()` 什么都不发生。你要是想用同一条链处理第二个请求,得先把 `index_` 重置回 0(上面那个 `MiddlewareChain` 没暴露 reset,你得自己加)。这和指针链不一样——指针链是无状态的(每次 `handle` 都从头开始),洋葱模型是有状态的(游标会前进)。在 Web 框架里这通常通过「每个请求 new 一个 chain」来解决,但你要知道这个状态性的存在,不然在复用 chain 时会莫名其妙地「链怎么不跑了」。

更隐蔽的坑:**如果同一个中间件里调了两次 `c.next()`,`index_` 会前进两次**,后面的中间件顺序就乱了。这种重入错误编译期查不出来,只能靠纪律约束。所以洋葱模型虽好,「每个中间件恰当地、只调一次 next()」这条纪律得守住。
:::

## 各种变种,什么时候该用哪个

到这里我们攒了三种主写法(指针链 / vector 调度器 / 中间件洋葱),还有几种在特殊场景才用得上的变种。把它们的适用面摆清楚:

| 写法 | 核心机制 | 适合 | 不适合 |
|---|---|---|---|
| `next_` 指针链 | 每个节点持 `next_`,处理或转发 | 节点少、链基本静态、教材式 CoR | 链要动态拼装、节点会增减 |
| `std::vector` 调度器 | 集合统一调度,`can_accept` 判断 | 流水线、广播、链形状要一眼看清 | 需要前后置逻辑、需要基于结果决定走不走 |
| 中间件洋葱 | `std::function` + `index_` 游标,节点持 `next` 引用 | Web 中间件、需要前置/后置/短路 | 只需简单判断就处理的场景(杀鸡用牛刀) |

除了这三种,还有几种变种,它们解决的是更细分的诉求:

**策略链(Chain + Strategy)**:节点不再是一个类,而是一个 `std::function<bool(const Request&)>`。好处是不用为每个节点写一个类,坏处是 `std::function` 有类型擦除开销(可能堆分配、有间接调用),而且逻辑散在一堆 lambda 里可读性会下降。适合节点逻辑很短、数量多的规则引擎场景。

**树形链**:请求不只往一个方向传,而是往多个子节点广播(典型如 GUI 事件冒泡)。它把链从一维直线扩展成了一棵树。我们这篇不展开,但你看到「事件从根窗口往子控件传」时,那就是树形链。

**环形链**:链尾连回链头,形成闭环。天然适合调度器、轮询这种「持续传球直到条件满足」的场景。**它必须有人为设的终止条件,否则就是无限循环**——这一点是硬约束,没商量。

**可回溯责任链**:链不仅支持正向传递,某个节点失败时还能沿链**反向回滚**,执行前面节点的补偿操作。这就是数据库事务、分布式 Saga 的模型。它本质上已经不是单纯的「请求传递」,而是「正向做事 + 反向撤销」的双向链。

## 踩坑预警:别被「异步责任链」骗了

::: warning 一个广为流传的「异步责任链」例子其实是串行的
讲责任链的中文资料里,经常能看到这样一个「异步责任链」的例子,大意是把每个 handler 包成返回 `std::future` 的函数,然后用 `std::async` 跑:

```cpp
class AsyncChain {
public:
    using Handler = std::function<std::future<void>()>;
    void add(Handler h) { handlers_.push_back(std::move(h)); }

    void run() {
        std::future<void> fut = std::async(std::launch::async, [this] {
            for (auto& h : handlers_) {
                h().get();   // <-- 关键:这里 .get() 会阻塞等当前完成
            }
        });
        fut.get();
    }
private:
    std::vector<Handler> handlers_;
};
```

这段代码**叫做「异步」,但它根本没让 handler 并发跑**。问题就在循环里那句 `h().get()`——`.get()` 是**阻塞**的,它会等当前这个 handler 的 future 完成才返回,然后循环才进入下一轮。也就是说,handler 是**一个接一个串行执行**的,`std::async` 只是把每个 handler 丢到了另一个线程上跑,然后立刻阻塞等它跑完。这和直接顺序调用 handler 几乎没有区别,反而多了线程切换的开销。

我们在编译器里验证这个判断——两个 handler 各 sleep 300ms,如果真并发,总耗时应该接近 300ms;如果是串行(像上面这段代码),总耗时应该接近 600ms:

```sh
$ g++ -std=c++23 -O2 -Wall chain_async.cpp -o chain_async
$ ./chain_async
Step 1 start
Step 1 done
Step 2 start
Step 2 done
--- total elapsed: 600 ms (serial ~600, concurrent ~300)
```

600ms。**实测就是串行的**。所以别被这个例子的名字骗了——它叫「异步责任链」,但它没有让链上的 handler 并发执行。真正的异步链应该让 handler 在不同的 future 上**同时**推进(比如先 `h()` 拿到所有 future,再统一 `.get()`),或者干脆用协程(`co_await`)来表达「等待这一步完成」。如果你看到一份资料把上面这段代码当成「责任链的异步版本」推荐给你,记住它的真实行为是串行的,别拿来当并发方案用。
:::

## 小结

我们把整条演进路径捋一遍:

| 阶段 | 做法 | 为什么还不够 |
|---|---|---|
| 调用方写死顺序 | 一长串 `if (!step) return;` | 处理者集合和调用方逻辑揉在一起,违背开闭,无法运行时重组 |
| `next_` 指针链 | 每个节点持 `next_`,`process` 返回 true 即停 | 调用方解耦了,但节点间耦合、插节点要重连、易成环 |
| `std::vector` 调度器 | 集合统一管,`can_accept` 判断 | 链收拢了,但节点失去转发自主权,广播 vs 首中即停要自己定 |
| 中间件洋葱 | `std::function` + `index_`,节点持 `next` 引用 | 表达力最强,但 chain 有状态、游标一次性,杀鸡用牛刀 |

记下这几条关键结论:

- 责任链要解决的是 **「让请求的发送方和接收方解耦」**:发送方不知道链上有谁、什么顺序,只把请求丢给链头,链自己决定传到哪停。GoF 的意图一句话就是「给多个对象处理同一个请求的机会,把它们串起来,直到有人处理」。
- 经典指针链用 **「模板方法 + 责任链」** 的组合根治了「每个子类都要记得转发」的坑:**非虚的 `handle` 焊死转发骨架,纯虚的 `process` 只回答「我处理了吗」**。这是写责任链时最容易写错的点,拆分做对了,转发逻辑只写一次。
- 指针链没你想的那么解耦——**它把耦合从「调用方 ↔ 处理器」搬到了「节点 ↔ 节点」**:插中间节点要重连指针、链形状散落在每个 `next_` 里、`shared_ptr` 互持容易成环泄漏。节点会动态增减的场景,改用 vector 调度器。
- vector 调度器和指针链的语义差别要分清:**广播(遍历所有匹配者)vs 首中即停(第一个吃掉就 break)**,这是 `dispatch` 循环里加不加 `break` 的事,配套工程用的是广播。
- 中间件洋葱(`std::function` + 游标)是表达力最强的写法,**前置/后置/短路全能做**,但它有状态(游标一次性),适合 Web pipeline 这种「每个节点既要前后置又要能短路」的场景,别在简单流水线上杀鸡用牛刀。
- **那个广为流传的「异步责任链」例子是串行的**——循环里 `h().get()` 阻塞等每个 handler 完成,handler 根本不并发。真正的异步链要靠协程或先收齐 future 再 `.get()`,别照抄那个串行例子。

::: tip 配套可编译工程
这一篇的 `Message` / `Handler` / `HandlerChain`(广播式 vector 调度器,`DiskHandler` / `ConsoleHandler` / `GuiHandler`)在本仓库里有完整的 CMake 工程,clone 下来一把就能跑:[ResponsibilityChain / OutputHandler](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/code/volumn_codes/vol4/design-patterns/ChainOfResponsibility)。仓库里的版本用的就是广播语义(遍历所有 handler,不 break),你可以顺手改成首中即停(`break`)体会两种语义的差异。
:::

## 参考资源

- [cppreference:`std::function`](https://en.cppreference.com/w/cpp/utility/function)(C++11 起,中间件洋葱里类型擦除的载体)
- [cppreference:`std::shared_ptr`](https://en.cppreference.com/w/cpp/memory/shared_ptr)(C++11 起,指针链里的 `next_` 节点持有方式)
- [cppreference:`std::future` / `std::async`](https://en.cppreference.com/w/cpp/thread/async)(C++11 起,异步链相关的并发原语;注意 `.get()` 是阻塞的)
- GoF,《Design Patterns: Elements of Reusable Object-Oriented Software》—— 责任链模式原始定义(Intent:避免请求发送方与接收方耦合)
- 配套可编译工程:[ResponsibilityChain](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/code/volumn_codes/vol4/design-patterns/ChainOfResponsibility)
