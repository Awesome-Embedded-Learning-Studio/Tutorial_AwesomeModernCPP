---
title: "观察者模式:从悬空指针到 weak_ptr 防悬挂"
description: "从最直觉的「被观察者持有一组裸指针」开始,一步步撞上观察者先死的悬空崩溃,再用 weak_ptr 把生命周期治理干净,顺手做出一个 RAII 订阅、snapshot 通知、可重入的线程安全事件源"
chapter: 11
order: 17
tags:
  - host
  - cpp-modern
  - intermediate
  - 观察者模式
  - weak_ptr
  - 回调机制
difficulty: intermediate
platform: host
cpp_standard: [11, 17, 20]
reading_time_minutes: 22
related:
  - "单例模式:从注释约束到 Meyer's Singleton"
prerequisites:
  - "Chapter 6: 类与对象"
  - "智能指针与所有权"
---

# 观察者模式:从悬空指针到 weak_ptr 防悬挂

## 我们到底在解决什么问题

我们先不上定义。想一个最常见的场景:你在写一个气象站,后台的 `WeatherForecast` 周期性地从传感器拿到新的温度湿度,然后需要把这套数据分发给好几个不同的展示端——一个控制台默认显示器、一个手机 App 推送端、一块电视墙大屏。这三个端长得完全不一样、迭代节奏也不一样,你当然可以把它们都写死在 `WeatherForecast` 内部,让气象站直接 `phone.show(...)`、`tv.show(...)` 地挨个调,但很快你就崩了:每加一个新端(比如再加个网页端),都得回头改气象站的源码;每删一个端,又得回来删一遍调用。气象站本该只关心「数据来了」这一件事,结果被迫认识世界上所有的显示器。

观察者模式要解决的就是这类需求:**让「数据源」和「关心数据变化的一群组件」彻底解耦,数据源只负责在状态变化时发一声广播,谁想听就来订阅,听了就走,数据源一个都不认识**。气象站、UI 事件总线、股票行情推送、按键状态变化通知——它们都有「一个变化,要通知一堆不知道是谁的听众」的天然诉求。

但「发一声广播」这六个字,在 C++ 里**绝对不是写个循环挨个调函数那么简单**。这里有一个 C++ 独有的、比任何别的语言都凶险的坑:**听众可能在气象站还在广播的时候就被销毁了**。Java 有 GC,Python 有引用计数兜底,而 C++ 的对象生命周期是手动管理的——一旦听众先死,气象站手里攥着的就是一个指向幽灵的指针,下一次广播就是访问已释放内存,程序要么崩,要么吐出一堆垃圾,要么在 ASan 下当场翻车。所以这一篇我们真正要回答的问题是——**怎么让被观察者能够广播变化,同时无论观察者何时被销毁,都不会出现悬空指针**。

接下来我们就一步步来,从最直觉的写法开始,看看每一步为什么不够,最后逼出一个现代 C++ 的标准答案。

## 第一步:最直觉的写法——被观察者持有一组裸指针

很多人第一次写观察者模式,下意识画出来的结构是这样:一个抽象的观察者接口,一个被观察者内部维护一个观察者列表,状态变化时遍历列表挨个回调。我们就拿 playground 里那个气象站当蓝本,先把它的骨架抽出来:

```cpp
struct MessagePackage {
    double temperature;
    double humidity;
};

// 观察者抽象接口:所有「想被通知的端」实现它
struct Sender {
    virtual ~Sender() = default;
    virtual void receiving_message(const MessagePackage& message) = 0;
};

// 几个具体的端
struct DefaultSender : Sender {
    void receiving_message(const MessagePackage& message) override {
        std::println("Receiving Message: Temperature: {}, Humidity: {}",
                     message.temperature, message.humidity);
    }
};

struct PhoneSender : Sender {
    void receiving_message(const MessagePackage& message) override {
        std::println("Hello from Message! Temperature: {}, Humidity: {}",
                     message.temperature, message.humidity);
    }
};

struct TVSender : Sender {
    void receiving_message(const MessagePackage& message) override {
        std::println("Hello from TV! Temperature: {}, Humidity: {}",
                     message.temperature, message.humidity);
    }
};
```

这部分没什么坑。多态接口是观察者模式的标配,`virtual ~Sender() = default` 保证通过基类指针析构时能正确走到派生类析构,这一行千万别漏——只要你的观察者会通过 `Sender*` 被销毁,缺了虚析构就是未定义行为。接下来是被观察者,先看最直觉的、用裸指针的写法:

```cpp
class WeatherForecast {
public:
    void register_observer(Sender* o) { observers_.push_back(o); }
    void detach_observer(Sender* o) {
        observers_.erase(std::remove(observers_.begin(), observers_.end(), o));
    }
    void notify_once() {
        for (auto* o : observers_) {
            o->receiving_message(sensor_.get_message_pack());
        }
    }
private:
    struct WeatherSensor {
        static MessagePackage get_message_pack();
    };
    std::vector<Sender*> observers_;
};
```

你看,这就是观察者模式的最小骨架:`register_observer` 负责把一个观察者的地址塞进列表,`notify_once` 负责在状态变化时遍历列表挨个调。逻辑上完全正确,只要观察者一直活着,这段代码能跑得非常漂亮。

但问题恰恰就出在「只要观察者一直活着」这个前提上。我们写一个最小的使用场景,把一个栈上对象的地址注册进气象站,然后让这个对象先于气象站广播离开作用域:

```cpp
int main() {
    WeatherForecast forecast;
    {
        DefaultSender obs;          // 栈上对象
        forecast.register_observer(&obs);
        forecast.notify_once();     // 此时 obs 还活着,正常
    }                              // obs 离开作用域,栈帧回收
    forecast.notify_once();        // obs 里的指针悬空了 -> use-after-free
}
```

第二个 `notify_once()` 走进去,`observers_` 里还稳稳地躺着那个 `&obs`,可是 `obs` 早就被回收了。这一行就是访问已经释放的栈内存。口说无凭,我们用 AddressSanitizer 编译跑一下,看看它到底会发生什么。

## 这里先验证一下:悬空指针真的会炸吗

我们写一段最小的复现代码,故意让观察者先于通知离开作用域:

```cpp
#include <iostream>

struct Observer {
    virtual ~Observer() = default;
    virtual void on_event(int v) = 0;
};

class Subject {
public:
    void subscribe(Observer* o) { observers_[count_++] = o; }
    void notify(int v) {
        for (int i = 0; i < count_; ++i) observers_[i]->on_event(v);
    }
private:
    static constexpr int kMax = 4;
    Observer* observers_[kMax];
    int count_ = 0;
};

struct Loud : Observer {
    int id;
    explicit Loud(int i) : id(i) {}
    void on_event(int v) override { std::cout << "Loud " << id << " got " << v << "\n"; }
};

int main() {
    Subject s;
    {
        Loud obs(1);
        s.subscribe(&obs);
        s.notify(10);              // 此时 obs 活着,正常
    }                              // obs 离开作用域 -> 指针悬空
    s.notify(20);                  // use-after-free
}
```

用 ASan 编译,然后跑:

```sh
$ g++ -std=c++23 -O0 -fsanitize=address -g observer_dangle.cpp -o observer_dangle
$ ./observer_dangle
=================================================================
==89061==ERROR: AddressSanitizer: stack-use-after-scope on address 0x...030
READ of size 8 at 0x...030 thread T0
    #0 Subject::notify(int) observer_dangle.cpp:16
    #1 main observer_dangle.cpp:40
SUMMARY: AddressSanitizer: stack-use-after-scope observer_dangle.cpp:16
```

ASan 当场抓住一个 `stack-use-after-scope`——`notify` 在第 16 行解引用那个已经离开作用域的 `observers_[i]`,访问的是回收过的栈内存。这就是悬空指针的真实代价:在没开 ASan 的生产环境里,它可能表现为「读到一堆垃圾值」、可能表现为「偶尔段错误」、也可能表现为「在我机器上好好的,上 CI 就崩」——典型的、最难复现的那类 bug。**裸指针做观察者,只要生命周期对不上,这个坑早晚踩到。**

问题清楚了:我们缺的不是「能不能广播」的能力,而是「**观察者死了之后,被观察者手里的指针该怎么办**」的治理。下面我们就一步步把它治住。

## 第二步:让被观察者持有 `shared_ptr`——堵住了悬空,却养出了僵尸

既然坑在于「观察者先死、指针悬空」,那最直觉的治理办法就是把所有权也交出去——让被观察者持有一个 `shared_ptr<Observer>`,这样只要被观察者还活着,观察者就跟着活着,指针永远不会悬空。这正是 playground 蓝本里 `WeatherForecast` 的写法:

```cpp
class WeatherForecast {
public:
    void register_observer(std::shared_ptr<Sender> sender) {
        observers_.push_back(std::move(sender));
    }
    void detach_observer(std::shared_ptr<Sender> sender) {
        observers_.erase(
            std::remove(observers_.begin(), observers_.end(), sender),
            observers_.end());
    }
    void notify_once() {
        for (auto& each : observers_) {
            each->receiving_message(sensor_.get_message_pack());
        }
    }
private:
    struct WeatherSensor {
        static MessagePackage get_message_pack();
    };
    std::vector<std::shared_ptr<Sender>> observers_;
};
```

这一改,悬空指针确实没了——`shared_ptr` 的引用计数保证只要 vector 里还存着一份,对象就不会被析构。但事情到这里出现了新的矛盾,而且这个矛盾比悬空更阴险:**被观察者悄悄接管了观察者的所有权**,于是出现了「僵尸观察者」。我们看看 playground 的 `main` 是怎么用的:

```cpp
int main() {
    WeatherForecast forecast;
    forecast.register_observer(std::make_shared<DefaultSender>());
    forecast.register_observer(std::make_shared<PhoneSender>());
    forecast.register_observer(std::make_shared<TVSender>());
    forecast.notify_once();
}
```

注意这里 `std::make_shared<DefaultSender>()` 造出来的是一个**临时对象**,它传进 `register_observer` 之后,引用计数被 vector 里的那份接管,这时引用计数是 1——只有被观察者一个人持有。听起来没问题?我们验证一下它会导致什么:

```cpp
// 假设我们在某个函数里这样用
void run(WeatherForecast& forecast) {
    auto phone = std::make_shared<PhoneSender>();
    forecast.register_observer(phone);
    std::cout << "phone 还在,引用计数 = " << phone.use_count() << "\n";
}   // phone 离开作用域,外部引用没了
// 但 forecast 里的 shared_ptr 还在 -> PhoneSender 没死,成了"僵尸"
// 之后 forecast.notify_once() 仍会调用它
```

我们来实测一下这个僵尸行为:

```sh
$ g++ -std=c++23 -O2 -pthread observer_verify.cpp -o observer_verify && ./observer_verify
=== verify_zombie (strong ref keeps dead observer alive) ===
outside ref dropped next, but subject holds one
notify after outside dropped its ref:
  observer 3 got 999
(observer 3 was meant to die but subject kept it alive)
```

看,observer 3 在外部早就放弃引用了,本该死去,但因为被观察者持有一个 `shared_ptr`,它苟活了下来,继续被通知。这有两条严重的后果:**第一,生命周期被绑架了**——观察者什么时候死,不再由它的创建者说了算,而是被被观察者卡着,这在 UI 场景里特别要命(一个本该随窗口关闭销毁的视图,被事件源卡住不死,内存泄露+逻辑错乱);**第二,所有权语义被污染了**——`shared_ptr` 的本意是「共享所有权,最后一个拥有者放手时析构」,可观察者模式里被观察者根本不想拥有观察者,它只是想「在观察者还活着时能通知它」,这俩是完全不同的诉求。

更糟的是,这条思路把生命周期问题从一个极端推到了另一个极端:裸指针是「被观察者完全不管,观察者随便死,死了我也不知道」;`shared_ptr` 是「被观察者强行接管,观察者想死也死不掉」。我们要的不是这两个极端,而是中间那个微妙的平衡点——**被观察者知道观察者在不在,但不阻止它死**。

## 第三步:`weak_ptr` 防悬挂——观察不拥有,死了就知道

`weak_ptr` 就是为这个平衡点量身打造的。它的语义正是我们想要的:**不增加引用计数、不延长对象寿命,但能在任意时刻检查「对象还在不在」,在就借一份临时的 `shared_ptr` 用一下,不在就老老实实说没有**。把被观察者持有的引用从 `shared_ptr` 换成 `weak_ptr`,整个生命周期治理就豁然开朗:

```cpp
#include <memory>
#include <vector>

class WeatherForecast {
public:
    // 接受 shared_ptr,但只存 weak_ptr —— 不延长观察者寿命
    void register_observer(const std::shared_ptr<Sender>& sender) {
        observers_.push_back(sender);   // shared_ptr -> weak_ptr 隐式构造
    }
    void notify_once() {
        MessagePackage pack = WeatherSensor::get_message_pack();
        for (auto it = observers_.begin(); it != observers_.end(); ) {
            if (auto live = it->lock()) {        // 关键:试着把 weak 升级回 shared
                live->receiving_message(pack);   // 升级成功 -> 对象活着,通知它
                ++it;
            } else {
                it = observers_.erase(it);       // 升级失败 -> 对象已死,顺手清理
            }
        }
    }
private:
    struct WeatherSensor {
        static MessagePackage get_message_pack();
    };
    std::vector<std::weak_ptr<Sender>> observers_;
};
```

核心就一行 `it->lock()`。`weak_ptr::lock()` 是一个原子操作([util.smartptr.weak.obs]),它返回一个新的 `shared_ptr`:如果被管理的对象还活着,这个新 `shared_ptr` 指向它、引用计数加一;如果对象已经被析构,返回一个空的 `shared_ptr`。这里要先验证一下 lock 的行为是否符合我们的预期,因为整个模式的安全性全压在这一个语义上。

## 这里先验证一下:`weak_ptr::lock()` 到底怎么表现

我们写一个最小例子,把 `weak_ptr` 绑到一个 `shared_ptr` 上,分别在对象存活和已销毁两种情况下 `lock()`:

```cpp
#include <iostream>
#include <memory>

static void verify_weak_lock() {
    std::weak_ptr<int> w;
    {
        auto sp = std::make_shared<int>(42);
        w = sp;
        auto locked = w.lock();                 // 对象存活
        std::cout << "alive: use_count=" << locked.use_count()
                  << " value=" << (locked ? *locked : 0) << "\n";
        std::cout << "expired()=" << std::boolalpha << w.expired() << "\n";
    }                                           // sp 离开作用域,对象析构
    auto locked = w.lock();                     // 对象已销毁
    std::cout << "after destroy: locked.empty=" << (locked == nullptr)
              << " expired=" << w.expired() << "\n";
    if (auto p = w.lock()) {
        std::cout << "UNEXPECTED: got value " << *p << "\n";
    } else {
        std::cout << "lock failed -> skip callback (no crash)\n";
    }
}

int main() { verify_weak_lock(); }
```

编译跑一下:

```sh
$ g++ -std=c++23 -O2 -pthread observer_verify.cpp -o observer_verify
$ ./observer_verify
=== verify_weak_lock ===
alive: use_count=2 value=42
expired()=false
after destroy: locked.empty=true expired=true
lock failed -> skip callback (no crash)
```

看,对象存活时 `lock()` 返回的 `shared_ptr` 让引用计数变成 2(原来的 sp 加上 lock 出来的这一份),`expired()` 是 false;对象析构后,`lock()` 返回空指针,`expired()` 是 true,我们据此跳过回调,什么都不崩。这就是整个防悬挂模式的基石——**通知那一刻把 `weak_ptr` 临时升级成 `shared_ptr`,只要这个临时引用还在,对象就绝对不会在回调执行期间被析构**,这是 `weak_ptr` 给我们的硬保证。

我们再把完整的「观察者先死、通知照样安全」的场景跑一遍,确认防悬挂真的成立:

```sh
$ ./observer_verify
=== verify_weak_observer (weak_ptr prevents dangle) ===
notify with observer 2 alive:
  observer 2 got 100
notify after observer 2 destroyed:
(no crash: dead observers were skipped by lock)
```

observer 1 是临时对象、注册完立即析构,observer 2 在内层作用域析构——之后的通知对它们 `lock()` 全部失败,自动跳过,程序安安稳稳地继续。对比一下前面 ASan 那次 `stack-use-after-scope` 的翻车,同样是「观察者先死」,weak_ptr 版本连个喷嚏都不打。**这就是现代 C++ 治理观察者生命周期的标准答案:不拥有,但知道。**

## 第四步:RAII 订阅——析构即退订

到这一步,悬空指针解决了,但还有一个尾巴:观察者析构后,它的 `weak_ptr` 会留在 `observers_` 里,虽然每次通知都能识别出来并清理,但列表里会逐渐堆积一堆已死的弱引用,既占内存也让每次通知都白做一堆失败的 `lock()`。更优雅的做法是让观察者在**自己析构的那一刻**主动退订——但这里有个鸡生蛋问题:观察者析构时,它自己的 `this` 马上就要失效了,怎么退订?

答案是用一个**RAII 订阅令牌**。订阅不返回 void,而是返回一个对象,这个对象持有「退订需要的信息」(一个指向被观察者的指针 + 一个订阅 id),它的析构函数里完成退订。观察者把这个令牌存成成员,于是观察者析构时会先析构它的成员——令牌析构——退订完成,然后轮到观察者本体析构,顺序天然正确:

```cpp
#include <cstddef>
#include <functional>
#include <unordered_map>
#include <memory>
#include <mutex>

class WeatherForecast {
public:
    using Callback = std::function<void(const MessagePackage&)>;

    // RAII 订阅令牌:析构时自动退订
    class Subscription {
    public:
        Subscription() = default;
        Subscription(std::size_t id, WeatherForecast* owner)
            : id_(id), owner_(owner) {}
        Subscription(const Subscription&) = delete;
        Subscription& operator=(const Subscription&) = delete;
        Subscription(Subscription&& o) noexcept
            : id_(o.id_), owner_(o.owner_) { o.owner_ = nullptr; o.id_ = 0; }
        Subscription& operator=(Subscription&& o) noexcept {
            if (this != &o) { unsubscribe(); id_ = o.id_; owner_ = o.owner_;
                               o.owner_ = nullptr; o.id_ = 0; }
            return *this;
        }
        ~Subscription() { unsubscribe(); }
        void unsubscribe() {
            if (owner_) { owner_->detach_by_id(id_); owner_ = nullptr; id_ = 0; }
        }
    private:
        std::size_t id_ = 0;
        WeatherForecast* owner_ = nullptr;
    };

    // 订阅:把回调和 weak_ptr 一起登记,返回 RAII 令牌
    Subscription subscribe(Callback cb) {
        std::lock_guard<std::mutex> lk(mtx_);
        std::size_t id = next_id_++;
        callbacks_.emplace(id, std::move(cb));
        return Subscription{id, this};
    }

    void detach_by_id(std::size_t id) {
        std::lock_guard<std::mutex> lk(mtx_);
        callbacks_.erase(id);
    }

private:
    std::mutex mtx_;
    std::unordered_map<std::size_t, Callback> callbacks_;
    std::size_t next_id_ = 1;
};
```

这套设计把「退订」从一件需要程序员记得去做的事,变成了一件析构函数自动保证的事——只要令牌析构,退订一定发生。这和单例篇里讲 RAII 的精神是一致的:**把约束交给语言机制,而不是人脑约定**。观察者持有一个 `Subscription` 成员,它死,令牌跟着死,退订跟着完成,被观察者的列表里不会留下一具具弱引用的尸体。

不过这里我要诚实地告诉你一个权衡:RAII 订阅令牌用的是「id + 回调」模型,而不是「id + weak_ptr 到观察者对象」模型,原因在于 `std::function` 回调可以捕获任意状态(包括 `weak_ptr<Observer>`),比硬绑死一个观察者接口更灵活——你既能写面向对象的 `subscribe([this](auto& p){ view_.on_change(p); })`,也能写完全无对象的纯函数式回调。代价是 `std::function` 会做一次堆分配(以及类型擦除带来的潜在内联失败),在性能极端敏感、通知频率极高的场景里,回到裸 `Observer*` 接口 + `weak_ptr` 那套会更省。多数业务场景这点开销完全可以忽略。

## 踩坑预警:在 notify 里改订阅列表

::: warning 踩坑预警
你迟早会遇到这种场景:某个观察者在自己的回调里,根据收到的内容决定「我已经不需要听了,我要退订」,或者「我要再注册一个新观察者」。听起来很合理,但如果你在 `notify` 遍历 `observers_` 的过程中直接 `erase` 或 `push_back`,就会触发迭代器失效——要么崩溃,要么漏调几个观察者,要么重复调。

问题的根源是:**通知路径和增删路径操作的是同一个容器**。一边在迭代,另一边在改结构,STL 容器不保证在这种并发修改下还能正常工作。正确的做法是**snapshot 通知**:进入 `notify` 时,先在锁内把当前所有回调拷贝一份到本地 vector,然后**释放锁**,在锁外遍历这份拷贝挨个调用。这样回调里想怎么改 `observers_` 都行——它改的是原表,而通知遍历的是拷贝,两者互不干扰。通知结束后再统一应用这一轮里积攒的增删(通常放进 `pending_add_` / `pending_remove_` 两个缓冲区,等最外层 `notify` 退出时一并处理)。
:::

我们验证一下 snapshot 这条路走得通:

```cpp
#include <iostream>
#include <functional>
#include <vector>

class Subject {
public:
    using Cb = std::function<void(int)>;
    void subscribe(Cb cb) { observers_.push_back(std::move(cb)); }

    // snapshot 版:先拷一份再调用,回调里随便改原表
    void notify_good(int v) {
        std::vector<Cb> snap(observers_);     // 拷一份
        for (auto& cb : snap) cb(v);          // 遍历拷贝
    }
private:
    std::vector<Cb> observers_;
};

int main() {
    Subject s;
    int hits = 0;
    s.subscribe([&](int v){ ++hits; std::cout << "A got " << v << "\n"; });
    s.subscribe([&](int v){ ++hits; std::cout << "B got " << v << "\n"; });
    s.notify_good(1);
    std::cout << "total hits = " << hits << " (expect 2)\n";
}
```

```sh
$ g++ -std=c++23 -O2 -pthread observer_reentry.cpp -o observer_reentry && ./observer_reentry
=== notify_good (snapshot) ===
A got 1
B got 1
total hits = 2 (expect 2)
```

两个观察者都被精确地调用了一次。snapshot 的代价是每次通知都要拷贝一份回调列表(`std::function` 拷贝是一次堆分配),所以这套方案适合「通知频率中等、观察者数量可控」的场景;如果通知频率极高,可以换成用不可变的 `shared_ptr<vector<Cb>>` 做写时复制,或者干脆走无锁的 RCU 路线——但那些已经超出观察者模式本身的范畴了。

## 踩坑预警:循环依赖,通知无限递归

::: warning 踩坑预警
还有一个更阴险的坑:**A 观察 B,B 又观察 A**。A 一变通知 B,B 的回调里改了 A,A 又通知 B,B 又改 A……这就是一个死循环,体现到程序上就是栈溢出(`StackOverflow`)或者 CPU 被一条事件链永久占满。

治理它的首选办法不是搞复杂的检测机制,而是在源头堵住——**在 `setX()` 之前先比较新旧值,只有真正变化时才通知**。这是最朴素也最有效的一招,因为它把「无意义的自我触发」从根上掐掉了:

```cpp
class Person {
public:
    void set_age(int new_age) {
        if (age_ == new_age) return;   // 值没变,不通知,直接打断环路
        age_ = new_age;
        forecast_.notify_once();       // 真变了才广播
    }
private:
    int age_ = 0;
    // ...
};
```

除此之外还有几条辅助手段:把多个连续变化合并成一次通知(`begin_update()` / `end_update()` 事务模式,只在事务结束时广播);在回调路径上挂一个「通知抑制开关」(`ScopedNotificationDisable` 这种 RAII 守卫),在已知会触发环路的更新段里临时关掉通知;以及在设计层面就避免双向观察,如果非得双向,就明确谁是主数据源、谁是被动端,被动端绝不在回调里改主端。**真碰上环路,先做变更检测,这是性价比最高的一步。**
:::

## 实战:一个能跑的气象站事件源

我们把前面所有治理手段——`weak_ptr` 防悬挂、RAII 订阅、snapshot 通知、变更检测——揉到一起,做一个真能用的 `WeatherForecast`。它周期性从传感器取数据,温度湿度任一变化就通知所有订阅者;订阅者随便什么时候销毁都不会让气象站崩,订阅者随便在回调里增删订阅都不会让通知崩溃:

```cpp
#pragma once
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

struct MessagePackage {
    double temperature;
    double humidity;
};

class WeatherForecast {
public:
    using Callback = std::function<void(const MessagePackage&)>;

    class Subscription {
    public:
        Subscription() = default;
        Subscription(std::size_t id, WeatherForecast* owner)
            : id_(id), owner_(owner) {}
        Subscription(const Subscription&) = delete;
        Subscription& operator=(const Subscription&) = delete;
        Subscription(Subscription&& o) noexcept;
        Subscription& operator=(Subscription&& o) noexcept;
        ~Subscription() { unsubscribe(); }
        void unsubscribe();
    private:
        std::size_t id_ = 0;
        WeatherForecast* owner_ = nullptr;
    };

    WeatherForecast() = default;

    Subscription subscribe(Callback cb);

    // 取一次数据并广播;温度或湿度之一变化才通知(变更检测)
    void poll_once();

private:
    void detach_by_id(std::size_t id);

    // 模拟传感器:实际项目里这里是硬件读取或网络拉取
    struct WeatherSensor {
        static MessagePackage get_message_pack();
    };

    std::mutex mtx_;
    std::unordered_map<std::size_t, Callback> callbacks_;
    std::size_t next_id_ = 1;
    MessagePackage last_{};          // 上一次的快照,用于变更检测
};
```

```cpp
WeatherForecast::Subscription WeatherForecast::subscribe(Callback cb) {
    std::lock_guard<std::mutex> lk(mtx_);
    std::size_t id = next_id_++;
    callbacks_.emplace(id, std::move(cb));
    return Subscription{id, this};
}

void WeatherForecast::detach_by_id(std::size_t id) {
    std::lock_guard<std::mutex> lk(mtx_);
    callbacks_.erase(id);
}

void WeatherForecast::Subscription::unsubscribe() {
    if (owner_) { owner_->detach_by_id(id_); owner_ = nullptr; id_ = 0; }
}

WeatherForecast::Subscription::Subscription(Subscription&& o) noexcept
    : id_(o.id_), owner_(o.owner_) { o.owner_ = nullptr; o.id_ = 0; }

WeatherForecast::Subscription& WeatherForecast::Subscription::operator=(Subscription&& o) noexcept {
    if (this != &o) {
        unsubscribe();
        id_ = o.id_; owner_ = o.owner_;
        o.owner_ = nullptr; o.id_ = 0;
    }
    return *this;
}

void WeatherForecast::poll_once() {
    MessagePackage pack = WeatherSensor::get_message_pack();

    // 变更检测:温度湿度都没变,不通知(打断潜在的事件环路)
    bool changed = pack.temperature != last_.temperature
                || pack.humidity    != last_.humidity;
    last_ = pack;
    if (!changed) return;

    // snapshot:锁内拷一份回调,锁外调用
    std::vector<Callback> snapshot;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        snapshot.reserve(callbacks_.size());
        for (auto& kv : callbacks_) snapshot.push_back(kv.second);
    }
    for (auto& cb : snapshot) {
        try { cb(pack); } catch (...) { /* 单个观察者崩不波及整体 */ }
    }
}
```

用起来就是这样,各端想听就来、想走就走,气象站完全不关心它们是谁、什么时候死:

```cpp
int main() {
    WeatherForecast forecast;

    auto phone = std::make_shared<PhoneSender>();
    WeatherForecast::Subscription sub1 = forecast.subscribe(
        [wphone = std::weak_ptr(phone)](const MessagePackage& m) {
            if (auto p = wphone.lock()) p->receiving_message(m);
        });

    {
        auto tv = std::make_shared<TVSender>();
        WeatherForecast::Subscription sub2 = forecast.subscribe(
            [wtv = std::weak_ptr(tv)](const MessagePackage& m) {
                if (auto p = wtv.lock()) p->receiving_message(m);
            });
        forecast.poll_once();   // phone 和 tv 都收到
    }                           // sub2 析构 -> 自动退订

    forecast.poll_once();       // 只有 phone 收到,没有悬空、没有僵尸
}
```

注意回调里我们用的又是 `weak_ptr`——这是一个「双保险」:即使有人忘了把 `Subscription` 存成成员、即使退订因为某种竞态没及时生效,回调里 `lock()` 失败也会让它安静地跳过,而不是去碰一个已经析构的对象。**`weak_ptr` 在这里同时承担了「防悬挂」和「容错」两重角色。**

::: tip 配套可编译工程
这一节的完整工程在本仓库里,三种 `Sender`(默认/手机/电视)、传感器取数、注册与通知一条龙,clone 下来 cmake 一把就能跑:[Observer / WeatherForecast](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/code/volumn_codes/vol4/design-patterns/Observer/WeatherForecast)。
:::

## 观察者模式为什么不讨喜

到这里我们有了一个正确、生命周期治理干净、还线程安全的观察者实现。但事情到这里还没完——我得诚实地告诉你,观察者模式在工程里也有它自己的代价,用之前最好心里有数。

**第一,它是「隐式调用」。** 被观察者调谁、什么时候调,在源码里是看不出来的——你只看到 `notify()`,看不到谁会响应。这意味着调试时,一个观察者的回调莫名其妙被触发,你得在运行时去翻订阅列表才能知道是谁注册了它。这种「控制流的隐式跳转」是观察者模式最大的可读性代价,大规模使用会让代码变得难追。

**第二,通知顺序不可预期。** 如果你的逻辑对「谁先收到通知」有依赖(比如观察者 A 要在 B 之前更新,因为 B 依赖 A 的中间结果),那用 `unordered_map` 存回调的观察者实现会狠狠坑你一把——哈希表的遍历顺序是不确定的。哪怕换成 `vector`,你也得在文档里明确「顺序是注册顺序」,并且祈祷以后没人改这个假设。

**第三,异常吞咽。** 你看到上面的 `try { cb(pack); } catch (...) {}` 了——为了不让一个观察者的异常炸掉整个通知流程,我们吞掉了它。但这意味着观察者里的 bug 可能被静默吞掉,排查时根本不知道出过错。一个更负责任的做法是记日志,或者提供一个可配置的异常策略钩子,但无论如何,**「吞异常」本身就是一个不得不做的妥协**。

**第四,生命周期治理再干净,也挡不住「观察者还活着但状态不对」。** `weak_ptr` 只能告诉你对象在不在,不能告诉你对象处于什么状态。一个对象可能还活着,但它内部的资源已经失效(比如网络断开了、文件关了),观察者照样会被通知、照样会去用那个失效的状态。`weak_ptr` 治的是悬空,不是逻辑正确性。

## 小结

我们把整条演进路径捋一遍:

| 阶段 | 做法 | 为什么还不够 |
|---|---|---|
| 裸指针 | 被观察者持 `vector<Observer*>` | 观察者先死 -> 悬空指针 -> use-after-free |
| `shared_ptr` | 被观察者持 `vector<shared_ptr<Observer>>` | 接管所有权 -> 僵尸观察者,该死的死不掉 |
| `weak_ptr` 防悬挂 | 持 `vector<weak_ptr<Observer>>`,`notify` 里 `lock()` | **够用了**(死了就跳过,不悬空也不僵尸) |
| RAII 订阅令牌 | 析构即退订 | 观察者析构自动清掉自己的弱引用 |
| snapshot 通知 | `notify` 先拷贝再调用 | 治理回调里的增删导致的迭代器失效 |

记下这几条关键结论:

- **现代 C++ 写观察者,生命周期治理的首选是 `weak_ptr`**——被观察者持弱引用,`notify` 时 `lock()` 升级成临时的 `shared_ptr`,对象死了就跳过,既不悬空也不绑架所有权。
- **千万别让被观察者持有 `shared_ptr<Observer>`**,那是把观察者的所有权强行接管,会养出「该死死不掉」的僵尸观察者,污染所有权语义。
- **订阅一定要配 RAII 令牌**,让退订在观察者析构时自动发生,而不是靠人脑记得去调 `unsubscribe()`。
- **`notify` 必须走 snapshot**,否则回调里的增删会触发迭代器失效;碰上循环依赖,先在源头做变更检测。
- `weak_ptr` 治的是悬空指针,不是逻辑正确性——观察者状态对不对,它管不着。

## 参考资源

- [cppreference:`std::weak_ptr`](https://en.cppreference.com/w/cpp/memory/weak_ptr)(C++11,`lock()` / `expired()` / `use_count` 语义)
- [cppreference:`std::shared_ptr`](https://en.cppreference.com/w/cpp/memory/shared_ptr)(引用计数与控制块)
- [cppreference:`std::enable_shared_from_this`](https://en.cppreference.com/w/cpp/memory/enable_shared_from_this)(对象内部安全获取自身的 `shared_ptr`)
- 《设计模式》GoF,Observer 一节;Andréi Alexandrescu,《Modern C++ Design》第 5 章(泛化观察者)
- 配套可编译工程:[Observer / WeatherForecast](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/code/volumn_codes/vol4/design-patterns/Observer/WeatherForecast)
