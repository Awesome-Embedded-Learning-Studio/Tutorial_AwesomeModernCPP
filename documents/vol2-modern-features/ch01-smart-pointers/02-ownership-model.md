---
title: "资源所有权：独占、共享与借用"
chapter: 1
order: 2
description: "把“谁负责释放”讲清楚：所有权模型如何决定类型选择与函数签名"
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 17]
reading_time_minutes: 16
prerequisites:
  - "Chapter 1: RAII 深入理解"
  - "Chapter 0: 移动构造与移动赋值"
related:
  - "unique_ptr 详解：独占所有权的零开销智能指针"
  - "shared_ptr 详解：共享所有权与引用计数"
tags:
  - host
  - cpp-modern
  - intermediate
  - 内存管理
  - 智能指针
---
# 资源所有权：独占、共享与借用

咱们来看一段再普通不过的代码，里面的一个对象在函数之间被传来传去，每个人都拿到了它的指针，每个人都用了它：

```cpp
Session* load_session(const char* user);   // 返回一个裸指针

void handle_request(Session* s) {
    audit(s);
    cache_touch(s);
    // 用完了。删吗？audit 删？cache_touch 删？还是这里删？
}

void audit(Session* s)       { /* 只读，不删 */ }
void cache_touch(Session* s) { /* 只读，不删 */ }
```

您把 `delete` 写在哪都不踏实：写进了 `handle_request`，万一别的调用方还想用呢？您要是不写，堆上的 `Session` 就越攒越多。注释也救不了场。您写“调用方负责释放”，结果调用方不止一个，您写“被调用方负责释放”，结果被调用方只路过看了一眼。这段代码跑完了，释放的事归谁呢？您答不上来，其实也不怪您，咱们在整段代码里都找不到这个答案。

所有权（ownership）要补的就是这一块。它在语言里没有一个对应的保留字，标准库里也翻不出一个叫它的类，它是一份设计上的约定：一个资源从生到死的每一步归谁管，咱们得把它说得清清楚楚。C++ 的编译器不强制这套约定，不过类型系统能把答案编码进类型里。同样是“返回一个对象”的接口，写 `Session*` 是一种直白的答案，写 `std::unique_ptr<Session>` 的则是另一种，读代码的人拿到的信息就完全不同了。

上一篇咱们拆 RAII 的时候，解决的是“怎么释放”：析构函数绑定释放动作，离开了作用域就执行。上面这段代码缺的不是那个，缺的是“谁释放”。还有一种更隐蔽的毛病也得提防：好几个模块都存着这个指针，都认为自己是有份的，人人都持有的时候，也就等于谁都不负责了。这句话咱们讲到 shared_ptr 那篇还会再遇到一次，那边有个更狠的说法等着。

## 所有权到底是什么：一份没写进语言的约定

咱们把“所有权”这个词说实在了，免得它一直悬着：**一段代码持有了某个资源，就要对它的释放负责，咱们可以让它把责任接过来、转交出去，不过在任何时刻，责任总得落在某一个人的头上**。

C++ 语言本身是不认识这套约定的。`int* p = new int(42);` 在编译器的眼里没有任何歧义，也不携带“谁删”的信息，约束只能靠您记在脑子里、写进注释里、定进团队规范里。C++ Core Guidelines（Bjarne Stroustrup 和 Herb Sutter 牵头维护的编码规范集，资源管理那批条目的编号都带个 R，R 记的就是 Resource）把推荐做法写成了 R.1：

```text
R.1: Manage resources automatically using resource handles and RAII
```

咱们把它翻译过来，它的意思是用资源句柄和 RAII 自动管理资源。

上一篇咱们已经把 RAII 的机制拆过了：析构函数负责释放，“谁负责”被绑定到对象的生命周期上。R.1 说的就是，咱们别让资源裸着到处跑，给它套上一个会自动释放的句柄。所有权模型站在这句话的上层：句柄的形态不止一种，有独占的，有共享的，还有根本不拥有、只是借来用用的，它们的分工得理清楚。

Rust 把这件事做到了极端，所有权直接是语言的地基。咱们去翻 The Rust Book（Rust 官方教程）第 4 章，它给了三条规则：

```text
Each value in Rust has an owner.
There can only be one owner at a time.
When the owner goes out of scope, the value will be dropped.
```

咱们一条条翻译过来：每个值都有它的所有者，同一时刻在岗的只有一名，所有者一离开自己的作用域，值就被丢弃了。Rust 的编译器会强制执行它们，违反了直接编不过。C++ 是没这个待遇的，咱们靠规范加类型编码去逼近同样的效果。咱们不开 Rust 教程，咱们把规则借过来用一用，是想让您看到“所有权可以被当成一门语言的出发点”。C++ 把同样的问题交回给工程师，手里的工具就是类型系统。

理清楚的办法，是给“持有一个资源”的代码分角色。落到 C++ 的日常里，就是独占、共享、借用这样的三种持有关系，外加一个让所有权换手的动作：转移。接下来咱们挨个把它们过一遍。

## 独占：同一时刻，只有一个负责释放的人

独占（exclusive）是三种角色里最简单的一种：资源只有一个主人，主人没了，资源也就跟着释放了。咱们在标准库里给它的落点是 `std::unique_ptr`：

```cpp
auto a = std::make_unique<Widget>(7);
// auto b = a;          // 编译不过：unique_ptr 禁止拷贝
auto b = std::move(a);  // 所有权换手：b 接管，a 从此两手空空
```

咱们把两条性质合起来看：禁拷贝、可移动，这就是独占所有权的全部宣言。为什么要禁拷贝？您把拥有句柄复制一份，就有了两个“主人”，俩人都以为释放是自己的事，结局就成了同一块内存被删两遍。01 篇手写 `FileHandle` 的时候，咱们把拷贝构造 `= delete` 掉，防的就是这个事故，后面咱们还会专门看它一眼。

可移动的那一半，请您带着上一章学过的移动构造再看一遍。移动构造干的事，您还记得吧：把源对象的指针搬走、置空源对象。翻译成所有权的语言，就一句话：所有权从源换到了目标。执行了 `std::move(a)`，`a` 就不再拥有任何东西了，它只剩一个合法的空壳，您可以安全地析构它，给它赋上新值也是可以的。上一章里咱们练的是怎么写移动构造，这一篇把它升级成一个问题：**这一行执行完了，归谁负责释放呢？**您能不假思索地答出来，移动语义就算真正到手了。

## 共享：最后一个走的负责释放

有的资源确实不止一个主人：一份配置被三个子系统读、一条连接被两个任务用的场面，您多半见过。咱们不能让谁单方面决定它死，那就定下一条共同守则：大家一起持有，最后一个撒手的负责释放。这个角色的名字叫共享（shared），它的落点是 `std::shared_ptr`，靠引用计数数出了还有几位持有者，计数归零的时候，对象也就释放了。

共享不是白拿的。您每拷贝一份 `shared_ptr`、每销毁一份，都要付一次原子计数的开销，背后还养着一块堆上的控制块。这两笔开销的完整剖析，就留给 shared_ptr 那一篇了，这里咱们只要带着一个印象往下走：共享是有价的，有了真需求才值得付。什么叫真需求呢？咱们换个方向问：您拿起 `shared_ptr` 的理由，会不会只是“终于不用想所有权了”？要真是这么想的，您就别写它了。真需求是反过来的，它是您把所有权想过一遍、确认躲不开了，才落笔的：多个模块要**独立地**决定“我还在用它”，咱们谁也说不准别人是不是还用着。借用满足不了这样的场景，因为借用的前提，是对象在别人手里确定地活着。

## 借用：使用，但不负责释放

出场最频繁的，是第三种角色：咱们只是用一下资源，归谁管就不用咱们操心了。这个角色的名字叫借用（borrow，Rust 社区带火的叫法，C++ 这边也这么叫）。借用的载体您天天在写：

```cpp
void print(const Book& b);          // 引用：不可空，调用方保证对象在
Book* find(const std::string& k);   // 指针：可空，没找到就给 nullptr
```

咱们把 Core Guidelines 给两个载体各写的原文放到一起看：

```text
R.3: A raw pointer (a T*) is non-owning
R.4: A raw reference (a T&) is non-owning
```

咱们翻译过来：裸指针和裸引用都不拥有资源。

请您留意两条规则的措辞：它没说裸指针坏，说的是它**不表达所有权**。您在签名里写 `T*`，读代码的人就该把它理解成“我只是借来用用”。写 `T&` 的道理相同，只是另加了一条“保证不为空”。两者怎么选呢？判据其实就一条：可空性。咱们想表达“可能没有”（查找失败、可选配置），您就用 `T*`，进了函数记得判空。其余的场合用 `T&`，把非空的保证写进类型里。

borrow 家族还有两位更专门的成员：`string_view` 和 `span`，它们把“借用”做成了正经的视图类型。`string_view` 的内部长什么样，等到了视图那一章再拆开。`span` 的专文还要更靠后，落在标准库那一卷的容器部分。这里咱们把名字挂上就行。

> 咱们还要认识一下 GSL（Guidelines Support Library，配合 Core Guidelines 用的支持库，微软在 GitHub 上维护）里的 `owner<T>`，它的定义就一行：
>
> ```cpp
> template <typename T> using owner = T;
> ```
>
> 咱们看到的就是一个纯类型别名，运行期的额外开销是零，多一个字节的负担都没有。它是写给工具看的：咱们用 `owner<int*> p = new int(42);` 标一下，clang-tidy（一个静态检查工具）的 `cppcoreguidelines-owning-memory` 检查就有了依据。它逮的是类型层面的违规：`new` 出来的东西落进了非 owner 的裸指针、`delete` 了一个非 owner、把返回 owner 的结果交给不拥有的变量，咱们犯到哪一样，它都肯替咱们点名。边界咱们也得说清：类型它只认声明的，数据流它是不追的。泄漏、释放之后接着用这类要顺着执行路径才看得见的毛病，它就管不着了。在裸指针的世界里，这是咱们能给静态分析留的最小线索。

## 转移：move 就是所有权换手

三种持有关系都讲完了，剩下的那个动作叫转移（transfer）。您其实已经认识它了，它就是咱们熟悉的 move。上一章里咱们一行一行写过移动构造，指针搬走、源置空的两步，您还记得吧。现在咱们给这套动作正式定名，它的名字就是**所有权换手**：源对象从“拥有”换成了“不拥有”，目标对象反了过来，中间没有第二个主人插足的瞬间。

被移动过的那个对象，它的状态是“有效但未指定”——这是上一章给过的原话。用所有权的话复述一遍：它不再拥有任何资源，但您可以安全地析构它，您也可以给它赋上一个新值，所有权就又回到它手里了。唯一别做的，是去读它的值。

转移在代码里出没的位置，您以后可以对着找，三处都落在拥有所有权的句柄上：显式的 `std::move` 是一处，拥有句柄的按值传参是一处，`unique_ptr` 交给按值的形参，所有权就换到了函数手里，从函数返回的 `unique_ptr` 又是一处（RVO 说的就是返回值优化，细节咱们在上一章专门开过一篇）。您扫到这些行，心里过一句“所有权换手了”，释放责任的新主人是谁，也就跟着读出来了。

## 签名就是所有权文档

咱们把约定写在注释里，注释是会过期的。咱们把约定写进函数签名，签名就不会撒谎了——您改了签名，所有的调用处都得跟着动，编译器会一处处替您点名。所以一份签名能承载的所有权信息，值得咱们认真对待。Core Guidelines 的 R 系列里有一批条目专门管参数怎么传，咱们挑最常用的过一遍。

咱们从接手这一类说起，Core Guidelines 给它起的名字叫 **sink**，它的直译是“接收端”，东西倒进来了，就留在了函数这边。有一类函数天生就是干这个的：您把任务塞进队列、把连接交给管理器，干的就是这个。咱们去看 R.32 的原文：

```text
R.32: Take a `unique_ptr<widget>` parameter to express that a function assumes ownership of a `widget`
```

咱们翻译过来：按值收 `unique_ptr<widget>` 参数，说的就是“函数接手了 widget 的所有权”。等真要落到代码上的时候，咱们就让参数按值收：

```cpp
void enqueue(std::unique_ptr<Task> t) {   // sink：队列接手，任务的死活从此归队列
    queue_.push_back(std::move(t));       // 再转交给容器
}

enqueue(std::make_unique<Task>(42));      // 造出来直接交出去，中间不落地
```

您作为调用方，看到按值的 `unique_ptr` 参数，义务就清清楚楚了：把它 move 了进来，然后就可以忘掉它了。

另一类函数要干的事更刁一点：它当的既不是 sink、也不是 borrow，它要**修改调用者手里那个智能指针的指向**。比如重新加载配置、重新建立连接的活，说的就是这一类。它的叫法是 **reseat**，字面的意思是“重新落座”，干的事就是换掉指针指着的对象。咱们接着看 R.33 的原文：

```text
R.33: Take a `unique_ptr<widget>&` parameter to express that a function reseats the widget
```

咱们翻译过来：收 `unique_ptr<widget>&` 参数，就是在声明“函数会换掉 widget 指的对象”。

```cpp
void reload(std::unique_ptr<Config>& cfg);   // reseat：cfg 还归调用方，只是会被换个对象
// 函数体内大致是：cfg = std::make_unique<Config>(...);
```

这里的差别请您看仔细：`unique_ptr<Widget>&` 是对智能指针本身的引用，所有权还攥在调用方的手里，对象还稳稳地待在调用方手里，函数只是把它指的东西换掉了。sink 和 reseat 的名字长得像，语义隔着的却是一整个所有权模型，签名上差的只是一个 `&`。

咱们只管用用的这一类叫 **borrow**，绝大多数的函数都属于它。参数用的是 `const T&` 或 `T*`，判据还是咱们在借用一节给过的可空性。从签名里读出的义务同样清楚：函数不碰所有权，进去的时候什么也没带走。

函数往回返东西的时候，同样也是在表态的，返回值的说法有两种。工厂函数返回的是 `unique_ptr<T>`，是把造好的对象**连所有权一起交出**，新对象的返回默认就走这个路子。返回 `T*` 或 `T&` 则是**借出**：调用方拿到的只是使用的许可，您可绝不能 delete 它。您回头想想 `find` 的返回值就明白了：对象还在容器的管辖里，调用方要是手一滑删了，容器里就躺了个悬空指针。

智能指针身上还有一对容易看走眼的操作，咱们把它们放在一起认一认：`p.get()` 是借出，把内部的裸指针递给别人看看，所有权是纹丝没动的。`p.release()` 干的是弃管，智能指针撒了手，裸指针还给您，释放的责任就全压在您身上了：

```cpp
auto p = std::make_unique<Widget>(7);

Widget* peek = p.get();      // 借出：p 仍然拥有，没人该删 peek
Widget* raw  = p.release();  // 弃管：p 变空，raw 的释放归调用方
delete raw;                  // 弃管之后，释放只能自己来
```

挨得近的两个名字，干的事完全两样，您用之前请多看一眼，别看串了。连同 `reset()` 的完整边界，都留给 unique_ptr 那一篇了。

咱们光对着签名看，还是隔着纸面的。接下来咱们把构造、析构全部打印出来，真的跑一遍，看看对象最后是在谁手里断的气。三种签名各来一段：borrow 的只读，sink 的按值接手，还有工厂随返回值交出的 move-out：

```cpp
// GCC 16.2.1, -O2 -std=c++17
#include <iostream>
#include <memory>
#include <utility>

struct Task {
    explicit Task(int id) : id_(id) {
        std::cout << "Task(" << id_ << ") 构造\n";
    }
    ~Task() {
        std::cout << "~Task(" << id_ << ") 析构\n";
    }
    int id() const { return id_; }
private:
    int id_;
};

// borrow：只读一下，不碰所有权
void report(const Task& t) {
    std::cout << "report 看到了 Task " << t.id() << "\n";
}

// sink：按值收，函数接手
void finish(std::unique_ptr<Task> t) {
    std::cout << "finish 接手了 Task " << t->id() << "\n";
}   // t 在这里析构——对象死在函数里

// 工厂：造好之后，连所有权一起交出（move-out）
std::unique_ptr<Task> make_task(int id) {
    return std::make_unique<Task>(id);
}

int main() {
    std::cout << "--- 场景一：borrow ---\n";
    {
        auto t = make_task(1);
        report(*t);              // 借用：用完就还
    }                            // t 离开作用域，unique_ptr 析构 Task

    std::cout << "--- 场景二：sink ---\n";
    {
        auto t = make_task(2);
        finish(std::move(t));    // 按值传参，所有权换手进函数
    }                            // t 已经两手空空，这里无事发生

    std::cout << "--- main 收尾 ---\n";
    return 0;
}
```

演示程序就放在下面了，您点“动手试一试”直接跑：

<OnlineCompilerDemo
  title="动手验证：所有权换手的全程追踪"
  source-path="code/examples/vol2/52_ownership_trace.cpp"
  description="构造与析构全部打印。看两个位置：borrow 的析构落在 main 的作用域末尾；sink 的析构落在 finish 函数体内——对象死在谁手里，签名早就写好了。"
  run-options="-O2 -std=c++17"
  allow-run
/>

拿到了输出，咱们对着数一遍：场景一里，Task 1 的析构跟在 `report` 后面、要等作用域收尾了才出现，借用可是一下都没碰所有权的，释放留在了原地。等到了场景二，Task 2 的析构出现在 `finish` 的内部，“finish 接手了”打印完它就断气了，回到 main 的时候什么都不剩了。`make_task` 扮演的是第三种签名：对象随着返回值交了出去，落在了调用方的手里。类还是原来的类，工厂还是原来的工厂，对象的死活落在哪一行，您拿三段签名逐个对回去，是不是一行都没岔开呢？

## 三起事故：模型被打破的代价

角色都分清楚了，咱们再反过来看：每一种角色被用错了，各有各的典型翻法。编译器在咱们这些现场，多半是帮不上忙的。它从来没被咱们要求过强制这套约定，拦不住的东西，就落到了运行期。咱们一起看完三起事故。

### 事故一：借用活过了对象，悬垂

借用的前提是对象活着，这一点咱们都认。可是这个前提一旦破了，借出去的指针就成了悬垂指针（dangling pointer），借出去的引用也一样悬垂：名字倒是还在，指的东西没了。最经典的写法，**就是返回局部变量的地址**：

```cpp
// GCC 16.2.1, -O2 -std=c++17
#include <iostream>

const int* find_answer() {
    int local = 42;    // 局部变量，住在这次调用的栈帧（函数调用占用的那块栈内存）里
    return &local;     // 函数一返回，栈帧连同 local 一起作废
}

int main() {
    const int* p = find_answer();
    std::cout << *p << "\n";   // 未定义行为：读一块作废的栈内存
    return 0;
}
```

GCC 对这个写法倒是不会沉默，编译的时候它就会警告您“返回了局部变量的地址”。至于跑起来的输出是 42、是垃圾、还是干脆崩掉，它可什么都没承诺，咱们不猜，实测会给咱们答案的。您把上面的代码存成 `dangling.cpp`，咱们编一遍、跑一遍：

```text
$ g++ -O2 -std=c++17 dangling.cpp
dangling.cpp: In function ‘const int* find_answer()’:
dangling.cpp:5:12: warning: address of local variable ‘local’ returned [-Wreturn-local-addr]
$ ./a.out
Segmentation fault
```

您借出 `T&` 或 `T*` 之前，值得停下来问一句：对方用它的这段时间里，原对象**保证**还活着吗？“应该活着”不算数的，咱们要的是“保证活着”。比如对象由调用方持有、由更外层的 RAII 句柄管着，这样的才作数。您要是答不上来，那就别借了，换个方式交接吧。

### 事故二：拷贝了拥有句柄，双重释放

第二起事故的主人公，是咱们当中最想省事的那个人。他造了一个裸指针，转头喂给了两个 `unique_ptr`：

```cpp
Widget* raw = new Widget(7);

std::unique_ptr<Widget> a(raw);   // a 认为自己负责释放
std::unique_ptr<Widget> b(raw);   // b 也认为自己负责释放
// 离开作用域：a 析构删一遍，b 析构再删一遍——同一块内存 delete 两次
```

咱们把这一幕看清：两个 `unique_ptr` 互不知道对方的存在，各自忠实地执行了释放职责，凑在一起就成了 double free（双重释放），未定义行为说的就是它。在 glibc 这类主流分配器上常见的结局，是分配器检测到了重复回收，程序也就直接终止了。运气差一点的时候，第二次 `delete` 破坏的是别人正在用的内存，事故现场能飘到很远的地方去。

这个例子咱们没有放进在线演示：双重释放本身就是未定义行为，它在演示平台上崩成什么姿势，都没有参考的价值。您只需要带走一句话——`unique_ptr` 禁拷贝防的就是它，可上面这个做法绕过了禁拷贝，因为构造函数拿到裸指针的时候，无从知道这个指针是不是已经有别的句柄在管着。01 篇的 `FileHandle` 把拷贝直接 `= delete` 了，等于把同样的防线手写了一遍。所有权的交接要走正门：move、按值传参、返回值。别去走“裸指针配对构造”的侧门。

### 事故三：所有权真空，谁都没删

第三起的事故最安静，咱们把现场看完就明白了：咱们看不到崩溃和警告，对象就悄悄地躺在堆上，直到进程退出的那一刻。两位当事人用裸指针做了交接，都以为删除是对方的事：

```cpp
// GCC 16.2.1, -O2 -std=c++17
#include <iostream>
#include <string>

struct Session {
    explicit Session(std::string tag) : tag_(std::move(tag)) {
        std::cout << "Session(" << tag_ << ") 构造\n";
    }
    ~Session() {
        std::cout << "~Session(" << tag_ << ") 析构\n";
    }
    const std::string& tag() const { return tag_; }
private:
    std::string tag_;
};

// 甲方：造出对象，裸指针交出去，觉得自己只是转交
Session* build_session() {
    return new Session("无主");
}

// 乙方：拿到指针用一下，觉得不是我 new 的、不归我删
void use_session(Session* s) {
    std::cout << "用了一下 Session " << s->tag() << "\n";
}

int main() {
    Session* s = build_session();   // 交接发生，但没有任何一方声明负责
    use_session(s);
    std::cout << "main 结束\n";
    return 0;                       // 到这里也没有 delete
}
```

事故三的现场不吵不闹，咱们也真的跑了一遍，您点“动手试一试”就能看到：

<OnlineCompilerDemo
  title="动手验证：所有权真空的泄漏现场"
  source-path="code/examples/vol2/53_ownership_leak.cpp"
  description="数一数打印行数：构造有、使用有、收尾有，唯独析构缺席——没人 delete，对象从未被释放，程序却安静地跑完了。"
  run-options="-O2 -std=c++17"
  allow-run
/>

```text
Session(无主) 构造
用了一下 Session 无主
main 结束
```

咱们把输出摆在眼前数：咱们看到构造一行、使用一行、收尾一行，唯独少了析构那一行。析构的那一行呢？它缺席了。`Session` 还躺在原来的堆上，可日志已经收尾了。这就是泄漏的全部现场，安静到有点发毛的地步。

咱们把真空和双重释放放在一起看，它们是一对货真价实的反义词，病因却是一样的：“拥有与否”这件事没写进类型。写明了独占，编译器既会拦着您拷贝，又会替您释放。等写成裸指针交接的时候，两头就都没了着落，全靠两位当事人的默契——而默契这个东西，刚入职的新同事是带不来的。

## 拿到代码怎么问：这行过后，谁负责释放

三种持有关系都到齐了，转移这个动作也归了位，咱们把它们压成一套能执行的流程。您面对任何一个新对象、任何一次交接的时候，就按下面的顺序过：

1. 咱们默认按独占起步：`unique_ptr`（或自带 RAII 的值类型）把它管起来，要交接就靠 move，转移动作说的就是它。
2. 真需要共享，咱们才上 `shared_ptr`。什么算真需要呢？共享一节给过判据：多个模块要独立决定“我还在用”。
3. 其余一切访问，咱们都按借用来办：`const T&` 或 `T*`，按可空性挑。

笔者自己项目里的体感是这样的：独占和共享的比例常年在九比一附近晃，九成的对象从头到尾只有一个主人。这个数字您不用背下来，方向才是要紧的——咱们从独占出发，让共享退到例外的位置上，让它去证明自己真的必要。等咱们讲到 shared_ptr 那篇，这个判断还会放进具体的场景里再过一遍。

这个流程好不好用呢，咱们得拿真的代码练一练。下面这个书架的例子，咱们一行一行问过去：

```cpp
class Shelf {
    std::vector<std::unique_ptr<Book>> books_;   // 成员：馆藏，Shelf 独占
public:
    void add(std::unique_ptr<Book> b) {          // 参数按值：sink，接手
        books_.push_back(std::move(b));          // 入库：所有权进容器
    }
    const Book* find(const std::string& title) const {
        for (const auto& b : books_) {
            if (b->title() == title) {
                return b.get();                  // 借出：容器仍然独占
            }
        }
        return nullptr;                          // 找不到：可空，所以返回指针
    }
};
```

咱们从 `books_` 这一行问起：每本 `Book` 的释放归谁管呢？负责的是 Shelf，载体是容器里的 `unique_ptr`，等 Shelf 析构的时候，它们会被一本一本地释放。`add` 的参数 `b` 呢？所有权就换到了函数手里，`push_back` 又把它换进了容器，这一行走完了，调用方对这本书就再没有义务了。`find` 的返回值呢？它的类型是 `const Book*`，按 R.3 的口径，它是不拥有的角色——您可以用它，但您别删它，对象归 `books_` 里的 `unique_ptr` 管。三个问题都问完了，这段代码的释放责任一行一行全有了着落。

以后您读别人的代码、review 同事的改动，这个问题就是手边最省事的探针：您在哪一行答不上来，哪一行就是嫌疑的现场。答不上来的次数多了，就说明这个代码库等着补一套写进类型的所有权约定，光靠注释是救不回来的。

上一篇结尾咱们留了个话头：所有权模型立住之后，`unique_ptr` 和 `shared_ptr` 就成了同一套思想的两种落地，再也不算两件零散的工具了。现在模型立住了。下一篇咱们去看头一种——`unique_ptr`，独占所有权的零开销落地：它是怎么把“禁拷贝、可移动”做进类型里的、怎么和容器与移动语义咬合的，还有 `release`、`reset` 这些手动操作的边界，都在那一边等着咱们。

## 参考资源

- [C++ Core Guidelines: Resource Management（R 系列）](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#S-resource)
- [The Rust Book, ch 4.1: What is Ownership?](https://doc.rust-lang.org/book/ch04-01-what-is-ownership.html)
- [GSL: Guidelines Support Library（owner 别名的出处）](https://github.com/microsoft/GSL)
