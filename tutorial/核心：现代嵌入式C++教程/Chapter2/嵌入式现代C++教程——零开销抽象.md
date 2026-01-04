# 嵌入式现代C++教程——零开销抽象

## 前言

我们经常会有一种感觉,也是大部分人第一时间反应的——复杂的代码抽象,会影响代码的执行时间。比如说,比起来类的使用,笔者真见过觉得不如直接使用零散的函数进行梭哈的朋友编写代码。因为他觉得使用类占用时间开销。

这其实是一个很普遍的误解。很多人一看到"面向对象"、"类"、"模板"这些词,就本能地认为这些东西肯定比C语言慢。毕竟,抽象嘛,听起来就像是在原本简单的代码上包了好几层,怎么可能不慢呢?

不知道是不是Bjarne Stroustrup说的,我没考证,但是这句话的确存在道理——**"你不需要为你不使用的东西付出代价,你使用的东西不可能手写得更好。"** 所以,C++的高级抽象特性(如类、模板、内联函数等)在编译后不应产生额外的运行时开销,其性能应该与手写的底层代码相当。这是C++的追求。

说白了,就是我们希望C++这门语言编写的代码,跟我们去手写汇编的效率是近乎一致的,前者的可维护性还要更好。这听起来有点像"鱼和熊掌兼得",但这恰恰是C++设计的初衷——给你高级的抽象能力,但不让你为此付出性能代价。

#### 为什么在嵌入式系统中重要?

在桌面应用或者服务器开发中,我们可能对几个时钟周期的差异不太敏感。但是在嵌入式系统里,情况就完全不同了。

嵌入式系统通常有严格的资源限制:

- **有限的CPU性能** - 每个时钟周期都很宝贵。很多MCU可能只跑在几十MHz,不像你的电脑动不动就是几个GHz
- **受限的内存** - ROM/RAM容量有限。可能整个程序只有几十KB的Flash,几KB的RAM
- **实时性要求** - 必须在确定的时间内完成任务。延迟几毫秒可能就会导致系统失效
- **功耗限制** - 额外的指令意味着更多功耗。电池供电的设备,多执行一条指令就多消耗一点电

所以在嵌入式开发中,我们既想要代码好维护、好理解,又不能牺牲性能。零开销抽象让我们能够使用现代C++特性提高代码可维护性,同时不牺牲性能。这就是为什么我们要好好理解这个概念。

## 实际案例分析

理论说完了,我们来看看实际的代码。毕竟我们都知道一句非常经典话——`talk is cheap, show me the code`。

#### 例子：GPIO控制

我们挺容易写这样的代码:

```cpp
// 直接操作寄存器
#define GPIO_PORT_A ((volatile uint32_t*)0x40020000)
#define PIN_5 (1 << 5)

void set_pin() {
    *GPIO_PORT_A |= PIN_5;  // 容易出错,魔法数字
}
```

这种写法有什么问题呢?首先,到处都是魔法数字。`0x40020000`是什么?如果不看手册,你根本不知道。`PIN_5`虽然看起来有意义,但实际上它的定义`(1 << 5)`在代码里到处复制粘贴,一旦要改,就得全局搜索替换。

更糟糕的是,这种写法没有任何类型安全。你可以把一个完全不相关的地址传进来,编译器不会报错。你甚至可以不小心写成`*GPIO_PORT_A = PIN_5`,直接把整个寄存器覆盖掉,而不是设置某一位。

但是在C++中,我们可以做的更加安全:

```cpp
// 类型安全的抽象
template<uint32_t Address>
class GPIO_Port {
    static volatile uint32_t& reg() {
        return *reinterpret_cast<volatile uint32_t*>(Address);
    }
public:
    static void set_pin(uint8_t pin) {
        reg() |= (1 << pin);
    }
    
    static void clear_pin(uint8_t pin) {
        reg() &= ~(1 << pin);
    }
};

using GPIOA = GPIO_Port<0x40020000>;

void set_pin() {
    GPIOA::set_pin(5);  // 类型安全,可读性强
}
```

看起来代码变多了对吧?但是仔细想想,这些"多出来"的代码其实都是模板定义,在编译时就会被处理掉。最终生成的机器码,和前面那个C版本是一模一样的!

可以试一试,笔者在之前的测试中甚至发现开销比C更小——因为编译器在优化模板代码时,有更多的上下文信息可以利用。

更重要的是,现在你有了类型安全。`GPIO_Port<0x40020000>`和`GPIO_Port<0x40020400>`是两个完全不同的类型,不会混淆。而且所有的操作都通过明确的接口进行,不会有意外覆盖寄存器的情况。

#### 例子：状态机实现

状态机在嵌入式系统里太常见了。按键处理、协议解析、电机控制……到处都是状态机。

**C风格(使用switch-case)**

传统的C语言实现,我们都写过:

```cpp
enum State { IDLE, RUNNING, STOPPED };
State current_state = IDLE;

void process_event(int event) {
    switch(current_state) {
        case IDLE:
            if(event == START) current_state = RUNNING;
            break;
        case RUNNING:
            if(event == STOP) current_state = STOPPED;
            break;
        case STOPPED:
            if(event == RESET) current_state = IDLE;
            break;
    }
}
```

这种写法简单直接,但是有几个问题。首先,状态和事件处理逻辑都混在一个大函数里,状态一多就很难维护。其次,添加新状态需要修改多处代码。最重要的是,编译器很难对这种动态的switch-case进行深度优化。

**零开销C++抽象(使用编译时多态)**

我们可以用C++的编译时多态来实现:

```cpp
// 编译时多态 - 无虚函数开销
template<typename StateImpl>
class State {
public:
    auto handle_event(int event) {
        return static_cast<StateImpl*>(this)->on_event(event);
    }
};

class IdleState : public State<IdleState> {
public:
    auto on_event(int event) { /* ... */ }
};

class RunningState : public State<RunningState> {
public:
    auto on_event(int event) { /* ... */ }
};

// 使用std::variant实现零开销状态切换
using StateMachine = std::variant<IdleState, RunningState, StoppedState>;
```

这个看起来复杂,但是魔法在于这是**编译时多态**,不是运行时多态。注意我们用的是CRTP(Curiously Recurring Template Pattern),而不是虚函数。编译器在编译期就知道每个状态的具体类型,可以直接生成针对性的代码,不需要虚函数表查找。

配合`std::variant`,我们还能在编译期确保状态切换的类型安全。而且`std::variant`的实现通常也是零开销的——它本质上就是一个联合体加一个标记,跟你手写union是一样的。

#### RAII资源管理

RAII(Resource Acquisition Is Initialization)是C++里一个非常强大的概念。在嵌入式系统里,我们经常需要管理各种资源:时钟、中断、DMA通道……

**手动管理(容易泄漏)**

先看看手动管理的问题:

```cpp
void configure_peripheral() {
    enable_clock();
    configure_pins();
    // 如果这里异常,时钟不会被禁用!
    do_something();
    disable_clock();
}
```

这个代码看起来没问题,但是有个隐患:如果`do_something()`里出了问题(虽然在嵌入式里我们通常不用异常,但可能有其他形式的错误处理),或者你在中间某个地方提前return了,`disable_clock()`就不会被执行。时钟一直开着,白白浪费功耗。

**零开销RAII**

用RAII的思想,我们可以这样写:

```cpp
class ClockGuard {
    uint32_t peripheral_id;
public:
    ClockGuard(uint32_t id) : peripheral_id(id) {
        enable_clock(peripheral_id);
    }
    ~ClockGuard() {
        disable_clock(peripheral_id);  // 自动清理
    }
};

void configure_peripheral() {
    ClockGuard clock(PERIPH_GPIOA);
    configure_pins();
    do_something();
    // clock自动析构,即使发生异常
}
```

这个写法的妙处在于,无论你的函数怎么退出——正常返回、提前返回、甚至异常——`ClockGuard`的析构函数都会被调用。这是C++语言保证的。

关键是,编译器会内联构造函数和析构函数,生成的代码与手动管理相同!你获得了自动资源管理的便利,但没有付出任何性能代价。这就是零开销抽象的精髓。

## constexpr - 编译期计算

`constexpr`是现代C++里一个杀手级特性。它让你可以在编译期进行计算,而不是在运行时。

```cpp
// 运行时计算(浪费CPU)
uint32_t calculate_baud_divisor(uint32_t cpu_freq, uint32_t baud) {
    return cpu_freq / (16 * baud);
}

// 编译期计算(零运行时开销)
constexpr uint32_t calculate_baud_divisor(uint32_t cpu_freq, uint32_t baud) {
    return cpu_freq / (16 * baud);
}

// 这个值在编译时计算,直接嵌入代码
constexpr uint32_t DIVISOR = calculate_baud_divisor(72000000, 115200);
```

你可能会想,这有什么区别?不就是加了个`constexpr`关键字吗?

区别大了去了!第一个版本,每次调用都要执行除法运算。除法在很多MCU上是比较慢的操作,可能需要几十个时钟周期。

而第二个版本,编译器在编译时就把结果算出来了。最终的机器码里,`DIVISOR`就是一个常量,直接写在代码里,不需要任何计算。这对于嵌入式系统来说是巨大的优势——既节省了CPU时间,又让代码的执行时间变得可预测(对实时系统很重要)。

更妙的是,你可以写很复杂的`constexpr`函数,包括循环、条件判断等等。只要参数在编译期已知,编译器就能算出结果。这让你可以把很多配置计算放在编译期完成,而不是在每次启动时计算。

## 实战技巧

讲了这么多理论,我们来看看一些实用的技巧。这些都是笔者在实际项目中用过的,确实能提升代码质量,而且不影响性能。

### 1. 使用内联函数替代宏

宏是C语言时代的产物。在C++里,大部分情况下你都应该用内联函数替代宏。

```cpp
// 不推荐:宏没有类型检查
#define MAX(a, b) ((a) > (b) ? (a) : (b))

// 推荐:内联函数零开销且类型安全
template<typename T>
inline constexpr T max(T a, T b) {
    return (a > b) ? a : b;
}
```

宏的问题太多了。首先,它没有类型检查,你传什么进去都行。其次,它有很多奇怪的副作用。比如`MAX(i++, j++)`,这个宏会展开成`((i++) > (j++) ? (i++) : (j++))`,结果i或j会被增加两次!

而内联函数不会有这些问题。编译器会做类型检查,而且参数只会被求值一次。同时,因为是`inline`的,编译器会直接把函数体插入到调用点,不会有函数调用的开销。

加上`constexpr`,如果参数是编译期常量,编译器还能在编译期就算出结果。这是宏做不到的。

### 2. 模板元编程

模板元编程听起来很高大上,但其实概念很简单:让编译器在编译期帮你做一些工作。

```cpp
// 编译期循环展开
template<size_t N>
struct UnrollLoop {
    template<typename Func>
    static void execute(Func f) {
        f(N-1);
        UnrollLoop<N-1>::execute(f);
    }
};

template<>
struct UnrollLoop<0> {
    template<typename Func>
    static void execute(Func) {}
};

// 使用
UnrollLoop<4>::execute([](size_t i) {
    process_data(i);  // 完全展开,无循环开销
});
```

这段代码做了什么?它在编译期把循环展开了。最终生成的代码相当于:

```cpp
process_data(0);
process_data(1);
process_data(2);
process_data(3);
```

没有任何循环结构,没有循环计数器,没有条件判断。对于小次数的循环,这种展开可以显著提升性能,因为避免了分支预测失败和循环开销。

当然,循环展开也不是银弹。如果循环次数很大,展开会导致代码膨胀。但对于嵌入式系统中常见的小循环(比如处理几个通道的ADC数据),这是个很好的优化手段。

### 3. 强类型替代原始类型

类型安全不只是为了防止错误,它还能让代码更清晰。

```cpp
// 易错:单位混淆
void delay(uint32_t time);  // 是毫秒还是微秒?

// 零开销强类型
struct Milliseconds { uint32_t value; };
struct Microseconds { uint32_t value; };

void delay(Milliseconds ms);
void delay_us(Microseconds us);

// 编译期检查,运行时无开销
delay(Milliseconds{100});  // 清晰明确
```

看第一个版本,`delay(100)`——这个100是什么单位?你得去看文档或者注释。而且你很容易搞混:

```cpp
delay(1000);  // 想延迟1秒,但如果delay是微秒单位就惨了
```

用强类型就不会有这个问题。`delay(Milliseconds{1000})`清清楚楚告诉你这是1000毫秒。而且如果你不小心写成`delay(Microseconds{1000})`,编译器会直接报错,因为类型不匹配。

关键是,这些强类型在运行时是完全零开销的。`Milliseconds`本质上就是一个`uint32_t`,编译器会把这层包装完全优化掉。你获得了类型安全,但没有任何性能损失。

## 验证零开销——眼见为实

说了这么多"零开销",你可能会想:真的吗?怎么证明?

最直接的方法就是看汇编代码。不要怕看汇编,其实没那么复杂。你只需要对比C版本和C++版本生成的汇编是否相同即可。

### 使用Compiler Explorer

笔者强烈推荐使用 Compiler Explorer (https://godbolt.org/)。这是一个在线工具,可以实时看到你的代码编译成什么样的汇编。

你可以写两个版本的代码:

- 左边写C风格的代码
- 右边写C++抽象的代码

然后对比两边生成的汇编。如果汇编完全相同(或者只有微小的差异),那就证明了这个抽象是零开销的。

### 本地验证

如果你想在本地验证,可以用这个命令:

```bash
# 编译时查看汇编
arm-none-eabi-g++ -O2 -S -fverbose-asm code.cpp
```

`-O2`表示开启优化(这很重要,零开销抽象依赖编译器优化),`-S`表示生成汇编文件,`-fverbose-asm`会在汇编里加上注释,更容易看懂。

### 关键编译选项

说到优化,这里有几个重要的编译选项:

```bash
-O2 或 -O3    # 优化级别,至少要O2
-flto         # 链接时优化,可以跨编译单元优化
-fno-rtti     # 禁用RTTI(运行时类型识别),嵌入式常用
-fno-exceptions  # 禁用异常,可选(很多嵌入式项目会禁用)
```

**重要提示**:`-O0`或者没有优化的情况下,很多零开销抽象会有开销。这是因为编译器没有做内联、没有做常量折叠等优化。所以测试零开销抽象,一定要开优化!

在实际嵌入式项目中,你的Release编译配置应该总是开启至少`-O2`优化的。Debug配置可以用`-Og`(优化debug体验)或者`-O0`。

## 笔者随意的碎碎念

####  "抽象总是有开销的"

不对。**正确的抽象在编译后是零开销的**。关键词是"正确的"——你要用编译期抽象(模板、内联函数、constexpr等),而不是运行期抽象(虚函数、动态分配等)。

很多人对抽象有偏见,是因为他们见过糟糕的抽象。比如到处用虚函数,到处用动态内存。这种抽象确实有开销。但这不是抽象本身的问题,而是用错了工具。

现代C++提供了大量编译期抽象工具,让你能写出既抽象又高效的代码。

#### "嵌入式必须用C"

这个观念已经过时了，但也没过时。不过现代C++完全适合嵌入式开发，而且有很多优势:

- 更好的类型安全
- 更好的资源管理(RAII)
- 更强大的编译期计算能力
- 更容易维护的代码

笔者见过太多用C写的嵌入式项目,代码里全是全局变量、魔法数字、重复的代码片段。这种代码很难维护,也很容易出bug。

用现代C++改写之后,代码量可能还会更少,而且更清晰。性能?完全不用担心,前提是你用对了特性。**但正是这个用对了特性**，让我对嵌入式使用C++产生悲观，C++用对特性，不是一件容易的事情。学习曲线的确更加陡峭。

#### "模板会增加代码体积"

对！但是这个要分情况看。模板会为每个使用的类型生成一份代码,所以如果你对100种类型实例化同一个模板,确实会增加代码体积。

但在实际嵌入式项目中,你通常不会这样做。而且很多时候,合理使用模板反而能**减少**代码体积,因为:

- 避免了代码重复
- 编译器可以更好地优化
- 可以用编译期计算替代运行时计算

笔者的建议是:不要盲目担心代码体积,先写出清晰的代码,然后编译看看实际大小。大部分情况下你会发现,模板版本的代码并不比手写版本大多少,甚至可能更小。

