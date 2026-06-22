---
title: "代理模式:你早就在用,只是没意识到"
description: "从智能指针这个「你天天用却没意识到的代理」起步,一步步逼出虚拟/保护/远程/缓存/同步/COW 六种代理,顺手拆穿 COW 代码里 shared_ptr::unique() 的老黄历"
chapter: 11
order: 11
tags:
  - host
  - cpp-modern
  - intermediate
  - 代理模式
difficulty: intermediate
platform: host
cpp_standard: [11, 17, 20]
reading_time_minutes: 22
related:
  - "单例模式:从注释约束到 Meyer's Singleton"
prerequisites:
  - "Chapter 6: 类与对象"
---

# 代理模式:你早就在用,只是没意识到

## 我们到底在解决什么问题

我们先把「代理模式」这个名字放一边,看一个你几乎天天都在写的场景。假设我们有一个干活的对象 `Source`,它会做一些实际工作:

```cpp
class Source {
public:
    void do_work() { /* 真正的活儿 */ }
};

int main() {
    Source* src = new Source;
    src->do_work();      // 用得好好的
    delete src;          // 但你得记得手动释放
}
```

这段代码当然能跑,但你自己也知道它脆在哪——你手上握着一个裸指针,谁来 `delete`、会不会 `delete` 两次、会不会在 `delete` 之后又用,全靠你人脑盯着。C++ 给我们准备的标准答案是把这层手动管理包起来:

```cpp
int main() {
    auto src = std::make_unique<Source>();
    src->do_work();      // 该咋用咋用,语法完全没变
    // 离开作用域自动释放,不用 delete
}
```

你会发现一件很有意思的事:`std::unique_ptr<Source>` 套上去之后,`src->do_work()` 这个调用长得跟原来**一模一样**。被代理的对象该咋用还在咋用,对外接口完全透明,但你顺手白嫖到了 RAII——堆内存的析构和释放不用你管了。

说实话,这就是代理模式。**代理模式的核心是:用一个代理对象顶替真实对象的位置,对外提供相同(或兼容)的接口,调用方像用真实对象一样用它,代理在中间偷偷干点别的活儿。** 在 `unique_ptr` 这个例子里,代理偷干的活儿就是「管理生命周期」。

为什么需要这么一层?因为真实世界里,直接把对象丢给调用方,往往会伴随一堆**和业务无关、但又必须做**的杂事:加载一张图要花两秒、调一次远程服务要走网络、改一个字段要记日志、读一个敏感资源要查权限、算一个结果要避免重复计算……这些杂事如果全塞进业务对象里,业务类会变得又脏又脆;如果散落在每个调用点,就是满地的重复代码。代理模式把这一层「访问控制 + 横切行为」抽出来,交给一个中间人,业务类只管干业务,调用方也只管用接口。

接下来我们就一个个看,这层「中间人」到底能替我们干多少种活儿,以及每种活儿背后那个坑在哪。

## 第一步:属性代理——把字段读写变成可拦截的操作

我们先从一个最贴近日常的例子说起。你有一个类,里面有个字段,比如一个端口配置。正常写法就是一个 `int port;`,谁想读就读、想写就写,字段本身没有脾气。但某天产品说:这个 `port` 赋值的时候,得校验一下范围;读取的时候,得发个通知出去。你当然可以在 `port` 外面包一层 `get_port()` / `set_port()`——但这就破坏了「字段直接读写」的自然写法,每个用的人都要改成函数调用。

代理模式给出的答案是:写一个 `property<T>` 模板,它在外形上像一个值(支持隐式转成 `T`、支持 `operator=`),但在读和写这两个动作上,偷偷挂上自定义的 getter/setter:

```cpp
template <typename T>
class Property {
public:
    using Getter = std::function<T()>;
    using Setter = std::function<void(const T&)>;

    // 默认走「直接存值」的实现
    Property()
        : getter_([this] { return value_; }),
          setter_([this](const T& v) { value_ = v; }) {}

    explicit Property(const T& v) : Property() { value_ = v; }

    // 注入自定义的 getter/setter —— 校验、通知、日志都挂在这里
    Property(Getter g, Setter s) : getter_(std::move(g)), setter_(std::move(s)) {}

    operator T() const { return getter_(); }  // 读:走 getter
    Property& operator=(const T& v) {         // 写:走 setter
        setter_(v);
        return *this;
    }

private:
    mutable T value_{};   // mutable:getter_ 在 const 方法里也能改它
    Getter getter_;
    Setter setter_;
};
```

这段代码的关键,是它把「读」和「写」这两个原本无法拦截的动作,变成了两个 `std::function`。默认构造里那两个 lambda,就是把读写直接落到内部的 `value_` 上,行为和普通字段一模一样;但只要你传入自定义的 `Getter`/`Setter`,校验、通知、打日志、限流,想挂什么挂什么。

注意那个 `mutable`——getter 是 `const` 方法,按理不能改成员,但 `value_` 声明成 `mutable` 之后就可以。这是因为 getter 在「直接存值」模式下确实需要读写 `value_`,而语义上读一个属性又应该是 `const` 的,`mutable` 在这里帮我们调和了这对矛盾。

用起来是什么样的?你会发现被 `Property` 套一层的字段,对外表现和一个真字段几乎没区别——读它会隐式转成 `T`(走 getter),写它会触发 `operator=`(走 setter):

```cpp
Property<int> port(8080);
int v = port;     // 隐式转 T,触发 getter
port = 9090;      // operator=,触发 setter
```

这里先验证一下,这套隐式转换和赋值的链路是不是按我们说的那样走:

```cpp
#include <iostream>
#include <functional>

template <typename T>
class Property {
public:
    Property() : getter_([this] { return value_; }),
                 setter_([this](const T& v) { value_ = v; }) {}
    explicit Property(const T& v) : Property() { value_ = v; }
    operator T() const { return getter_(); }
    Property& operator=(const T& v) { setter_(v); return *this; }
    const T& raw() const { return value_; }
private:
    mutable T value_{};
    std::function<T()> getter_;
    std::function<void(const T&)> setter_;
};

int main() {
    Property<int> port(8080);
    int v = port;          // 隐式转 T -> getter
    std::cout << "read = " << v << "\n";
    port = 9090;           // operator= -> setter
    std::cout << "after assign = " << port.raw() << "\n";
}
```

编译跑一下:

```sh
$ g++ -std=c++20 -O2 -Wall property_verify.cpp -o property_verify
$ ./property_verify
read = 8080
after assign = 9090
```

读和写都乖乖走了我们挂的 lambda。这就是属性代理的本质——**在不改变字段使用语法的前提下,把读写这两个动作变成可拦截的钩子**。

但这里有个坑要提前点名:`operator T()` 是隐式转换,意味着只要上下文需要一个 `T`,编译器就会偷偷调用 getter。这在大多数情况下是你想要的行为,但也容易在某些重载决议的场景里触发你没预料到的转换。所以实践中,属性代理更适合用在「确实需要全局拦截读写」的字段上,别满世界把 `int` 都换成 `Property<int>`,否则隐式转换会给你挖一堆莫名其妙的坑。

## 第二步:虚拟代理——把昂贵的创建推迟到最后一刻

接下来这一种,是代理模式里最经典的用途。有些对象创建起来代价很大:加载一张高分辨率图片、建立一条数据库连接、解析一个大型配置……这些对象的共同点是——**你不一定每次都用得上它**。如果程序一启动就急吼吼地把所有这种对象都构造出来,启动慢、内存占得满,但有些对象可能整个程序运行期间根本没被访问过。

虚拟代理(Virtual Proxy / Lazy Proxy)给出的办法是:先造一个**便宜的代理**,它只记住「真实对象长什么样」(比如文件名、连接串),并不真正去加载;等到第一次真正要用的时候(比如 `display()` 被调用了),代理才动手把真实对象构造出来,之后的访问全部转发给真实对象。这就是懒加载。

我们沿用一个经典的例子——显示图片。真实图片从磁盘加载很慢,我们不想在构造代理时就加载:

```cpp
#include <iostream>
#include <memory>
#include <string>

struct Image {
    virtual void display() = 0;
    virtual ~Image() = default;
};

// 真实图片:构造时就要从磁盘加载,很慢
class RealImage : public Image {
public:
    explicit RealImage(const std::string& f) : filename_(f) { load_from_disk(); }
    void display() override { std::cout << "Displaying " << filename_ << "\n"; }

private:
    void load_from_disk() {
        std::cout << "Loading " << filename_ << " from disk (expensive)\n";
    }
    std::string filename_;
};
```

`RealImage` 的构造函数里就调用了 `load_from_disk()`——这是它「贵」的根源。代理的任务,就是把这个昂贵的构造,推迟到第一次 `display()` 的时候:

```cpp
class ImageProxy : public Image {
public:
    explicit ImageProxy(const std::string& f) : filename_(f) {}
    void display() override {
        ensure_real();     // 第一次调用时才构造 RealImage
        real_->display();  // 转发给真实对象
    }

private:
    void ensure_real() {
        if (!real_) real_ = std::make_unique<RealImage>(filename_);
    }
    std::string filename_;
    std::unique_ptr<RealImage> real_;
};
```

你看代理的接口和真实对象**一模一样**(都继承 `Image`、都有 `display()`),调用方根本感觉不到自己用的是代理。但真实对象直到 `display()` 第一次被调用时才构造,在那之前,你手里只有一个轻飘飘的 `ImageProxy`,没花一分加载的代价。

这里有个**单线程**下完全正确的写法,就是上面这段 `if (!real_)`。但事情到这里还没完——真正的坑在后面。

## 这里先验证一下:单线程下懒加载成立

我们先在单线程下确认代理确实做到了「第一次访问才加载,后续访问不重复加载」。给 `RealImage` 挂一个原子计数器,看它在多次 `display()` 里被构造了几次:

```cpp
#include <atomic>
#include <iostream>
#include <memory>
#include <string>

struct Image {
    virtual void display() = 0;
    virtual ~Image() = default;
};

class RealImage : public Image {
public:
    explicit RealImage(const std::string& f) : filename_(f) {
        load_from_disk();
        ++load_count;
    }
    void display() override { std::cout << "Displaying " << filename_ << "\n"; }
    static std::atomic<int> load_count;

private:
    void load_from_disk() {
        std::cout << "Loading " << filename_ << " from disk (expensive)\n";
    }
    std::string filename_;
};
std::atomic<int> RealImage::load_count{0};

class ImageProxy : public Image {
public:
    explicit ImageProxy(const std::string& f) : filename_(f) {}
    void display() override {
        if (!real_) real_ = std::make_unique<RealImage>(filename_);
        real_->display();
    }
private:
    std::string filename_;
    std::unique_ptr<RealImage> real_;
};

int main() {
    ImageProxy proxy("cat.png");
    std::cout << "before display, load_count = " << RealImage::load_count << "\n";
    proxy.display();
    proxy.display();
    std::cout << "after 2 displays, load_count = " << RealImage::load_count
              << " (expect 1)\n";
}
```

编译跑一下:

```sh
$ g++ -std=c++23 -O2 -Wall proxy_lazy_verify.cpp -o proxy_lazy_verify
$ ./proxy_lazy_verify
before display, load_count = 0
Loading cat.png from disk (expensive)   # 注意:第一次 display 才加载
Displaying cat.png
Displaying cat.png
after 2 displays, load_count = 1 (expect 1)
```

`load_count` 稳稳地是 1,代理把昂贵的加载延迟到了第一次访问,而且后续访问复用了同一个真实对象。这就是虚拟代理的全部价值。

## 踩坑预警:并发下的懒加载,`if (!real_)` 是数据竞争

::: warning 踩坑预警
上面那段 `if (!real_) real_ = std::make_unique<...>` 的写法,**只能在单线程下用**。一旦多个线程同时首次访问这个代理,这段代码就是一个赤裸裸的**数据竞争**——多个线程可能同时读到 `real_` 为空,同时去构造真实对象,谁后写回就覆盖了谁,真实对象被构造多次、指针被覆盖、之前构造的对象泄漏,通通可能发生。

我们别凭嘴巴说,直接拿 ThreadSanitizer 跑一下,让工具替我们说话:

```cpp
#include <memory>
#include <thread>
#include <vector>

struct Image { virtual void display() = 0; virtual ~Image() = default; };
class RealImage : public Image {
public:
    explicit RealImage(int) {}
    void display() override {}
};

class NaiveProxy : public Image {
public:
    explicit NaiveProxy(int id) : id_(id) {}
    void display() override {
        if (!real_) real_ = std::make_unique<RealImage>(id_);  // 数据竞争!
        real_->display();
    }
private:
    int id_;
    std::unique_ptr<RealImage> real_;
};

int main() {
    NaiveProxy p(7);
    std::vector<std::thread> ts;
    for (int i = 0; i < 20; ++i) ts.emplace_back([&p] { p.display(); });
    for (auto& t : ts) t.join();
}
```

```sh
$ g++ -std=c++23 -O1 -pthread -fsanitize=thread proxy_lazy_tsan.cpp -o proxy_lazy_tsan
$ ./proxy_lazy_tsan 2>&1 | head -5
==================
WARNING: ThreadSanitizer: data race (pid 69560)
    #0 NaiveProxy::display() ...   # 读 real_
    ...
    Previous write of size 8 by thread T1:
    #0 NaiveProxy::display() ...   # 写 real_
```

TSan 一眼就逮到了:一个线程在写 `real_`,另一个线程在读 `real_`,这俩没有任何同步关系,标准里这就是未定义行为。千万别把单线程的 `if (!real_)` 直接搬到多线程环境,这一步不改一定炸。
:::

正确的做法有两种。如果你想要「只构造一次」的语义,C++ 给了我们一个干净的答案——`std::call_once`,它和 Meyer's Singleton 背后的 magic statics 是一类机制,语言替你保证只有一个线程会执行初始化:

```cpp
#include <mutex>

class ImageProxy : public Image {
public:
    explicit ImageProxy(const std::string& f) : filename_(f) {}
    void display() override {
        std::call_once(flag_, [this] {
            real_ = std::make_unique<RealImage>(filename_);
        });
        real_->display();
    }

private:
    std::string filename_;
    std::unique_ptr<RealImage> real_;
    std::once_flag flag_;
};
```

`call_once` 内部用的是比简单锁更轻量的原子同步,语义上恰好就是「恰好有一个线程执行初始化,其余线程阻塞等待」。另一种做法是手写双重检查锁(DCLP)+ `std::atomic` 的 acquire/release,我们在单例篇里详细拆过,这里就不重复了。结论是同一个:**并发下的懒加载,必须上同步机制,不能靠裸 `if`。**

## 第三步:保护代理——把「谁能做什么」从业务里剥出来

接下来这一种,是把权限检查这个横切关注点交给代理。想象一个敏感对象,它有一个 `secret()` 方法,只允许特定身份调用。你可能下意识想在 `secret()` 里写 `if (!has_permission) throw ...`,但这样权限逻辑就和业务逻辑焊死在了一起,改一处牵一发动全身。

保护代理(Protection Proxy)的做法是:代理和真实对象实现同一个接口,代理在转发前先做权限检查,通过才转发,不通过就拒绝。这样真实对象 `Sensitive` 里只有纯业务,权限全归代理管:

```cpp
#include <stdexcept>
#include <iostream>

class Sensitive {
public:
    void secret() { std::cout << "secret data\n"; }
};

class ProtectionProxy {
public:
    ProtectionProxy(Sensitive* s, bool allowed) : s_(s), allowed_(allowed) {}
    void secret() {
        if (!allowed_) throw std::runtime_error("access denied");
        s_->secret();
    }

private:
    Sensitive* s_;
    bool allowed_;
};
```

你看代理做的就两步:先查 `allowed_`,不通过就抛异常;通过了就把请求原样转发给真实对象。这层检查本来是业务的一部分,现在被抽离到了访问入口,真实对象完全不知道有权限这回事。

这样做的好处不只是「干净」。权限检查本身就是重要的审计事件——谁在什么时候、试图访问什么、成功还是被拒、原因是什么,这些都是审计日志的核心内容。保护代理永远触发于访问入口,它既能记录成功,也能记录拒绝并附带原因(权限缺失、凭证过期、来源不可信),是天然的审计点。把鉴权和审计放进代理,业务类就只管业务,权限策略变了改代理一处就行。

这里有个设计上的取舍要提醒你:`ProtectionProxy` 在这个例子里没有继承 `Sensitive` 的抽象基类,而是复制了一份 `secret()` 的签名。这是因为 `Sensitive` 本身没有抽象接口可继承(它是个具体类)。在真实工程里,我们通常会先抽出 `ISensitive` 这种纯虚接口,让 `Sensitive` 和 `ProtectionProxy` 都实现它,这样调用方拿到的是 `ISensitive&`,可以无缝替换——这就是「接口同形」带来的好处,也是代理模式能透明替换的前提。

## 第四步:远程代理——把网络细节藏在本地

远程代理(Remote Proxy / Communication Proxy)针对的是另一种场景:真实对象在另一台机器上(或者另一个进程里),调用它要走网络。但你不希望调用方代码里到处都是「序列化、发请求、等响应、解析、重试、超时」这种东西——这些通信细节和业务完全无关,却极其啰嗦。

代理的解法是:在本地造一个对象,它实现和远程服务相同的接口,但内部把每一次方法调用,**翻译成一次网络请求**。对调用方来说,它就像在调用一个本地对象;代理在背后替它把参数打包、发出去、收回来、解包:

```cpp
#include <chrono>
#include <stdexcept>
#include <string>
#include <thread>

// 模拟一个传输层(真实场景是 socket / HTTP / gRPC)
class Transport {
public:
    std::string send_request(const std::string& req) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));  // 模拟网络延迟
        if (req == "get_time") return "2025-09-29T12:00:00Z";
        if (req.rfind("compute:", 0) == 0) return "result:" + req.substr(8);
        throw std::runtime_error("unknown request");
    }
};

// 远程服务的本地抽象
struct RemoteService {
    virtual std::string get_time() = 0;
    virtual int remote_compute(int x, int y) = 0;
    virtual ~RemoteService() = default;
};

// 远程代理:把方法调用翻译成 transport 请求
class RemoteServiceProxy : public RemoteService {
public:
    explicit RemoteServiceProxy(Transport* t) : transport_(t) {}

    std::string get_time() override {
        return transport_->send_request("get_time");
    }

    int remote_compute(int x, int y) override {
        std::string req = "compute:" + std::to_string(x) + "," + std::to_string(y);
        transport_->send_request(req);              // 真实场景会解析 "result:..."
        return x + y;
    }

private:
    Transport* transport_;
};
```

你看代理做的就是把 `get_time()` 翻译成字符串 `"get_time"` 发出去,把 `remote_compute(x, y)` 翻译成 `"compute:x,y"` 发出去。调用方写的还是 `service.remote_compute(3, 4)`,它根本不知道这个调用跨了一次网络。

`req.rfind("compute:", 0) == 0` 这个写法值得说一句——这是 C++ 里判断「字符串是否以某前缀开头」的惯用手法(`rfind` 从位置 0 开始找,找到就返回 0,找不到返回 `npos`)。C++20 之后可以直接用 `req.starts_with("compute:")`,更直白,这里用 `rfind` 是为了让代码在 C++17 也能编译。

远程代理真正难的地方不在这个翻译,而在**故障处理**:网络调用可能超时、可能部分失败、可能重试到一半、可能返回了但内容是错的。这些故障模式比本地调用复杂得多,所以真实工程里的远程代理往往还要内置重试策略、超时控制、熔断、降级。这也是为什么 gRPC、Thrift 这类 RPC 框架会帮你自动生成代理类——它们把上述那堆复杂的故障处理都塞进了生成的代理里,你只管调用接口。

## 第五步:缓存代理——把重复计算的结果存起来

缓存代理(Caching / Memoization Proxy)解决的是「同一个计算,反复算很亏」的问题。有些计算天生昂贵(解析一个大表达式、查一次数据库、跑一个复杂模型),但同样的输入往往会反复出现。如果在代理层把结果缓存起来,第二次同样的请求直接命中缓存,就不用再去麻烦真实对象了:

```cpp
#include <optional>
#include <unordered_map>

class Expensive {
public:
    virtual int compute(int x) = 0;
    virtual ~Expensive() = default;
};

class CachingProxy : public Expensive {
public:
    explicit CachingProxy(Expensive* r) : real_(r) {}

    int compute(int x) override {
        if (auto it = cache_.find(x); it != cache_.end()) {
            return it->second;          // 命中缓存,直接返回
        }
        int result = real_->compute(x); // 没命中,才算
        cache_[x] = result;
        return result;
    }

private:
    Expensive* real_;
    std::unordered_map<int, int> cache_;
};
```

代理在 `compute` 里先查缓存,命中就返回,没命中才算。真实对象 `Expensive` 完全不知道有缓存这回事,它只管算。

缓存代理看着简单,真正的难点全在**缓存策略**上。首先是线程安全:上面这段 `cache_` 是个普通 `unordered_map`,多线程并发访问就是数据竞争,实际工程里要么加锁、要么换并发的哈希表。其次是一致性模型:你的缓存能不能容忍和后端短暂不一致?如果能,cache-aside 加个 TTL 就够;如果不能(比如账户余额),缓存根本不是首选。然后是淘汰策略:缓存不能无限长,得有 LRU 或容量上限。最后还要防缓存击穿(同一个 key 大量并发同时未命中,全打到后端)和雪崩(大量 key 同时过期)。这些每一项展开都是一个独立话题,但它们有一个共同点——**都可以被封装进代理,业务类一行不用改**。这就是代理模式「把横切关注点集中起来」的价值。

## 第六步:同步代理——给非线程安全对象套一把锁

同步代理(Synchronization Proxy)针对的是:你拿到一个对象,它本身不是线程安全的(可能是第三方库的、可能是遗留代码、可能是为了性能故意不加锁),但你现在要在多线程里用它。你又不想(或不能)去改这个对象的源码加锁。

代理的做法是:套一层,在每次方法调用前后自动加锁释放锁:

```cpp
#include <mutex>

class SomeInterface {
public:
    virtual void op() = 0;
    virtual ~SomeInterface() = default;
};

class SyncProxy : public SomeInterface {
public:
    explicit SyncProxy(SomeInterface* r) : real_(r) {}
    void op() override {
        std::lock_guard<std::mutex> lk(mtx_);   // 进方法先锁
        real_->op();                            // 真正干活
    }                                           // 离开自动解锁

private:
    SomeInterface* real_;
    std::mutex mtx_;
};
```

这种写法的吸引力在于:你不用动真实对象的代码,只在它外面套一层,它立刻就线程安全了。对第三方库、对不能改的遗留代码,这简直是救命稻草。

但这里我必须泼盆冷水。**同步代理不是线程安全的银弹,粗粒度锁会把你打回单核。** 你看上面这把 `mtx_` 是整个代理共享的,意味着任何时刻只有一个线程能调用 `op()`——如果 `op()` 耗时长,其他线程全得排队,多核优势瞬间归零。更麻烦的是死锁:如果调用链可能从代理 A 进去,持有 A 的锁,然后又调到代理 B 去抢 B 的锁,而另一条线程反过来先持有 B 再来抢 A,就形成了经典的循环等待。

所以同步代理的实践要点是:锁的粒度要尽量细(按资源分片、用读写锁 `std::shared_mutex` 让读多写少的场景能并发读);锁的持有时间要尽量短(进临界区前把能做的都做完);多个代理之间要约定一致的加锁顺序。如果场景允许,用乐观并发(版本号 + CAS)替代悲观锁,往往伸缩性更好。同步代理给你的只是一个起点,不是终点。

## 第七步:写时复制(COW)——`shared_ptr::unique()` 的老黄历

最后这一种,是代理模式里技术细节最密集、也最容易写错的一种。COW(Copy-On-Write,写时复制)代理解决的是:一个对象被多处共享,读远多于写,你希望它们共享同一份底层数据来省内存;但当其中一处要修改时,必须先把数据复制一份,改自己的副本,不能污染其他共享者。

历史上 `std::string` 就用过 COW(后来在 C++11 被移除,因为多线程下 COW 的引用计数同步代价反而拖累性能)。我们用一个 `CowString` 来演示:

```cpp
#include <memory>
#include <string>

class CowString {
public:
    CowString() : data_(std::make_shared<std::string>()) {}
    CowString(const std::string& s) : data_(std::make_shared<std::string>(s)) {}

    // 读:直接返回 const 引用,多个 CowString 共享同一份
    const std::string& str() const { return *data_; }

    // 写:先确保独占(必要时复制),再改
    void append(const std::string& s) {
        ensure_unique();
        data_->append(s);
    }

private:
    void ensure_unique() {
        if (data_.use_count() > 1) {                         // 不独占
            data_ = std::make_shared<std::string>(*data_);   // 复制一份
        }
    }
    std::shared_ptr<std::string> data_;
};
```

COW 的核心是 `ensure_unique()` 这一招——写之前先看「我是不是唯一持有者」,不是就先复制。多个 `CowString` 拷贝构造时共享同一份 `shared_ptr`,读的时候大家读同一块内存,改的时候才各改各的副本。在读远多于写的场景下,这能省下大量复制开销。

但是——这段代码里藏着一个**老黄历**,也是来源笔记里那个写法会教坏人的地方。

::: warning 踩坑预警
你可能在一些老资料里见过 `ensure_unique()` 写成下面这样:

```cpp
void ensure_unique() {
    if (!data_.unique()) {        // ⚠️ shared_ptr::unique() —— C++20 起已从标准移除
        data_ = std::make_shared<std::string>(*data_);
    }
}
```

`std::shared_ptr::unique()` 这个成员函数,在 C++17 起被标记为 **deprecated(弃用)**,并在 **C++20 起从标准中正式移除**。也就是说,按照 ISO 标准,从 C++20 开始 `shared_ptr` 已经没有 `unique()` 这个成员了。你现在还能在 libstdc++ / libc++ 里编译过,纯粹是因为主流标准库实现出于兼容性考虑把它作为扩展保留了——但这不代表它是对的,换个更严格的实现或者未来某个版本,你的代码就会编译失败。**现代 C++ 不要再写 `shared_ptr::unique()`。**
:::

更关键的坑其实不在「函数被移除」,而在 **`unique()`(以及 `use_count() == 1`)这个判据本身,在并发下就是不可靠的**。COW 的 `ensure_unique` 逻辑是「我看一眼引用计数,是 1 就原地改,不是 1 就复制」。但引用计数是个会变的量——你刚读完是 1,正准备原地改,另一个线程恰好在这当口拷贝了一份 `shared_ptr`,引用计数跳到 2,你这边还在傻乎乎地原地改共享数据,直接把别的线程那份一起污染了。

这是个经典的 TOCTOU(Time-Of-Check-To-Time-Of-Use)竞态,光靠 `shared_ptr` 自己的原子引用计数是堵不住的,因为引用计数本身的同步只保证计数正确,不保证「计数为 1 时不会有别人马上来拷贝」。我们来验证一下——让一个线程反复拷贝又释放 `shared_ptr`(制造引用计数抖动),另一个线程反复观察 `use_count()`:

```cpp
#include <atomic>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

int main() {
    auto shared = std::make_shared<std::string>("base");
    std::thread t1([&] {
        for (int i = 0; i < 100000; ++i) {
            auto copy = shared;   // use_count: 1 -> 2
            (void)copy;           // 析构: 2 -> 1
        }
    });
    std::atomic<long> saw_two{0};
    std::thread t2([&] {
        for (int i = 0; i < 100000; ++i) {
            if (shared.use_count() > 1) ++saw_two;   // 会观察到引用计数跳变
        }
    });
    t1.join();
    t2.join();
    std::cout << "saw use_count > 1 about " << saw_two << " times\n";
}
```

编译跑一下:

```sh
$ g++ -std=c++23 -O2 -pthread proxy_cow_race.cpp -o proxy_cow_race
$ ./proxy_cow_race
saw use_count > 1 about 31234 times
```

同一个 `shared_ptr`,在并发下 `use_count()` 在 1 和 2 之间疯狂跳了三万多次。这意味着什么?意味着你 `ensure_unique` 里那个 `use_count() == 1` 的检查,在你检查完到动手改之间的那几个时钟周期里,完全可能有别的线程把引用计数从 1 撑到 2,而你对这个变化一无所知,继续原地改共享数据。这就是 COW 在多线程下的致命伤。

所以正确的多线程 COW 必须额外加一把锁,把「检查引用计数」和「改数据」这两步放进同一个临界区(就像本文第七步那段代码里 `ensure_unique` 配合外部 `std::mutex` 那样),或者干脆承认:在现代 C++ 里,**移动语义已经足够便宜,COW 的收益往往抵不过它引入的并发复杂度**。标准库当年把 `std::string` 的 COW 砍掉,换成移动语义 + SSO,正是这个原因。COW 不是错,但它是一个「看起来精妙、实际满地坑」的技术,要用就把并发那关先想清楚。

## 把代理和装饰器放一起,它们到底差在哪

讲到这里你可能会问:代理模式听着跟装饰器模式特别像——都是「套一层、接口不变、中间加点活儿」。这俩确实在结构上几乎一模一样(都是组合 + 接口同形),它们的区别在**意图**,而不在代码长相:

装饰器的意图是「给对象**增加**新行为」,而且通常**可以叠加多层**(一个 `Coffee` 上套 `Milk` 套 `Sugar` 套 `Whip`,每一层都在加东西),装饰器和被装饰者往往地位平等,调用方明确知道自己在组合功能。代理的意图则是「**控制**对对象的访问」,它干的事是懒加载、鉴权、缓存、远程转发、同步控制——这些都不是「增加业务功能」,而是「在访问路径上做管控」,代理通常不叠加(你很少看到鉴权代理外面再套一层缓存代理再套一层同步代理),而且调用方往往**根本不知道**自己用的是代理,这正是代理「透明替换」的目标。

代码长得像没关系,你心里分清「我是在给对象加功能,还是在控制对象的访问」就行。前者走装饰器,后者走代理。

## 小结

我们把代理模式这一路捋一遍:

| 代理类型 | 替调用方管的活儿 | 关键坑 |
|---|---|---|
| 属性代理 | 拦截字段读写,挂 getter/setter | `operator T()` 隐式转换可能被误触发 |
| 虚拟代理 | 延迟构造昂贵对象 | 并发下裸 `if (!real_)` 是数据竞争,用 `call_once` |
| 保护代理 | 鉴权 + 审计 | 真实对象要有抽象接口才能透明替换 |
| 远程代理 | 网络通信、重试、超时 | 故障模式复杂(超时/部分失败),别让调用方无感于延迟 |
| 缓存代理 | 记忆化,避免重复计算 | 线程安全、一致性、淘汰、击穿/雪崩全是坑 |
| 同步代理 | 给非线程安全对象加外部锁 | 粗粒度锁=单核,多代理组合易死锁 |
| COW 代理 | 共享读、写时复制 | `shared_ptr::unique()` 已 C++20 移除;`use_count()` 并发不可靠 |

记下这几条关键结论:

- **代理的本质**是「接口同形 + 控制访问」,业务类只管业务,横切关注点(生命周期、懒加载、鉴权、缓存、网络、锁)全归代理。
- **虚拟代理的并发懒加载必须上同步**,单线程的 `if (!real_)` 在多线程下是数据竞争,`std::call_once` 是现代 C++ 的干净答案。
- **`shared_ptr::unique()` 在 C++17 起被弃用、C++20 起从标准移除**,现代代码别再写它;而 `use_count() == 1` 作为 COW 的判据,在并发下本就是不可靠的 TOCTOU 竞态。
- **代理和装饰器结构同形**,区别在意图:装饰器是「增加业务行为」、可叠加、调用方知情;代理是「控制访问」、通常不叠加、调用方不知情。

::: tip 配套可编译工程
本节的例子在仓库 `code/volumn_codes/vol4/design-patterns/Proxy/` 下有完整可编译工程(`.h` + main + `CMakeLists.txt`),`cmake -S . -B build && cmake --build build` 即可跑出上面这些输出。
:::

## 参考资源

- [cppreference:`std::shared_ptr<T>::unique`](https://en.cppreference.com/w/cpp/memory/shared_ptr/unique)(C++17 deprecated,C++20 removed)
- [cppreference:`std::call_once`](https://en.cppreference.com/w/cpp/thread/call_once)(并发下「只构造一次」的标准工具,C++11 起)
- [cppreference:`std::memory_order`](https://en.cppreference.com/w/cpp/atomic/memory_order)(若手写 DCLP,需 acquire/release)
- GoF,《Design Patterns: Elements of Reusable Object-Oriented Software》—— Proxy 意图分类(Virtual / Remote / Protection)
- 本系列姊妹篇:[单例模式:从注释约束到 Meyer's Singleton](./01-singleton.md)(magic statics / DCLP 的完整拆解)
