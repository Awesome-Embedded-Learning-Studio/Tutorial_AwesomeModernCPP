# 嵌入式C++开发——RAII 在驱动 / 外设管理中的应用

## 先问是什么

RAII（Resource Acquisition Is Initialization）来自 C++ 的兵器库：资源（文件、互斥、硬件句柄）在构造函数里被“拿到”，在析构函数里被“放回”。在嵌入式场景下，资源不是内存垃圾桶里的`new`/`delete`，而是：GPIO 引脚状态、SPI 的片选（CS）线、DMA 通道、文件描述符、外设时钟、互斥锁……这些东西忘了释放会导致外设卡死、功耗增加或系统不稳定。RAII 能把“释放”放到**作用域结束时自动执行**，大幅降低漏释放和状态不一致的概率。不过——嵌入式有限资源、可能无异常支持、ISR 环境特殊，所以用 RAII 时要注意约束：析构不能抛异常、不应做耗时阻塞操作、尽量避免在 ISR 中做复杂析构（或根本不要在 ISR 中创建短生命周期对象）。

------

## Example 1: 管理 GPIO 引脚（封装驱动的基本套路）

举个例子：咱们打算管一下GPIO的事情，咱们就创建一个 `GPIOPin` RAII 类，构造时设置方向/上拉/初始化电平，析构时安全地将引脚设为输入或安全状态。

```cpp
// gpio_raii.h
#pragma once
#include <cstdint>

enum class GPIODir { Input, Output };

class GPIOPin {
public:
    GPIOPin(uint8_t pin, GPIODir dir, bool init_level = false) noexcept
        : pin_(pin), dir_(dir)
    {
        // 假设底层 API：hal_gpio_config(pin, dir, pull, level)
        hal_gpio_config(pin_, dir_, /*pull=*/false, init_level);
        if (dir_ == GPIODir::Output) {
            hal_gpio_write(pin_, init_level);
        }
    }

    // 不可拷贝、可移动
    GPIOPin(const GPIOPin&) = delete;
    GPIOPin& operator=(const GPIOPin&) = delete;
    GPIOPin(GPIOPin&& other) noexcept
        : pin_(other.pin_), dir_(other.dir_), moved_(other.moved_)
    {
        other.moved_ = true;
    }
    GPIOPin& operator=(GPIOPin&&) = delete;

    ~GPIOPin() noexcept {
        if (moved_) return;
        // 将引脚恢复为安全态：输入（高阻）
        hal_gpio_config(pin_, GPIODir::Input, /*pull=*/false, /*level=*/false);
    }

    void write(bool v) noexcept {
        if (dir_ == GPIODir::Output) hal_gpio_write(pin_, v);
    }
    bool read() const noexcept { return hal_gpio_read(pin_); }

    uint8_t pin() const noexcept { return pin_; }

    // 手动放弃析构行为（比如资源被移交）
    void release() noexcept { moved_ = true; }

private:
    uint8_t pin_;
    GPIODir dir_;
    bool moved_ = false;
};
```

用法：

```cpp
void blink_once() {
    GPIOPin led(13, GPIODir::Output, /*init*/false);
    led.write(true);
    // 离开作用域时，led 自动恢复为输入（safe state）
}
```

注意：`hal_gpio_*` 是你实际平台的 HAL，实际实现要确保这些函数本身在中断上下文安全或不在 ISR 中被长时间调用。

------

## SPI 事务保护：`SPITransaction`（片选自动管理）

有时候我们可能会忘记放开片选（CS），导致从设备一直忙。用 RAII 把 CS 的 assert/deassert 与事务绑定。

```cpp
class SPIBus {
public:
    void beginTransaction() noexcept { /* 设置 SPI 控制寄存器、频率等 */ }
    void endTransaction() noexcept { /* 恢复 SPI 状态 */ }
    void transfer(const uint8_t* tx, uint8_t* rx, size_t n) noexcept {
        // 实际传输实现
    }
    void setCS(uint8_t pin, bool level) noexcept {
        hal_gpio_write(pin, level);
    }
};

// Guard: 构造时拉低 CS 并 beginTransaction；析构时拉高 CS 并 endTransaction
class SPITransaction {
public:
    SPITransaction(SPIBus& bus, uint8_t cs_pin) noexcept
        : bus_(bus), cs_pin_(cs_pin), active_(true)
    {
        bus_.beginTransaction();
        bus_.setCS(cs_pin_, /*active low*/false /*1? depends on hw*/);
    }

    SPITransaction(const SPITransaction&) = delete;
    SPITransaction& operator=(const SPITransaction&) = delete;
    SPITransaction(SPITransaction&& other) noexcept
        : bus_(other.bus_), cs_pin_(other.cs_pin_), active_(other.active_)
    {
        other.active_ = false;
    }

    ~SPITransaction() noexcept {
        if (!active_) return;
        bus_.setCS(cs_pin_, /*deassert*/true);
        bus_.endTransaction();
    }

    void dismiss() noexcept { active_ = false; }

private:
    SPIBus& bus_;
    uint8_t cs_pin_;
    bool active_;
};
```

用法（注意：放在函数作用域里）：

```cpp
void read_sensor(SPIBus& spi, uint8_t cs) {
    SPITransaction t(spi, cs);
    spi.transfer(tx_buf, rx_buf, len);
    // 自动释放 CS、结束事务
}
```

好处显而易见：任何 `return`、异常（若启用）或 early exit 都会正确释放 CS。

------

## DMA 通道 RAII

DMA 有“开始/等待/中止”流程。但是还需要注意的是——不要阻塞太久。

- 构造：分配/绑定 DMA 通道，配置描述符（但不启动），或者启动但返回前不会阻塞。
- 提供 `wait()` 或 `join()` 显式等待（阻塞可由调用者决定）。
- 析构：若 DMA 仍在运行，尝试中止（非阻塞）并做最小清理。

```cpp
class DMAChannel {
public:
    DMAChannel(uint8_t ch) noexcept : ch_(ch), running_(false) {
        hal_dma_allocate(ch_);
    }

    ~DMAChannel() noexcept {
        if (running_) {
            // 不要在析构中长时间等待，只执行非阻塞的中止
            hal_dma_abort(ch_);
            running_ = false;
        }
        hal_dma_free(ch_);
    }

    DMAChannel(const DMAChannel&) = delete;
    DMAChannel& operator=(const DMAChannel&) = delete;
    DMAChannel(DMAChannel&&) = delete;

    bool start(void* src, void* dst, size_t len) noexcept {
        running_ = hal_dma_start(ch_, src, dst, len);
        return running_;
    }
    // 可选：调用者显式等待（可能会阻塞）
    bool wait_until_done(unsigned timeout_ms) noexcept {
        return hal_dma_wait(ch_, timeout_ms);
    }

private:
    uint8_t ch_;
    bool running_;
};
```

不要在析构里 `wait_until_done()`，而在需要保证完成处显式调用 `wait_until_done()`。析构只做“尽可能安全的撤销”。

------

## 通用 `ScopeGuard`（处理 C 风格 API 与早返回）

RAII 不仅是硬件，也能包装“局部清理动作”。实现一个简单的 `scope_exit`：

```cpp
#include <utility>

template <typename F>
class ScopeExit {
public:
    explicit ScopeExit(F f) noexcept : func_(std::move(f)), active_(true) {}
    ~ScopeExit() noexcept { if (active_) func_(); }
    ScopeExit(ScopeExit&& o) noexcept : func_(std::move(o.func_)), active_(o.active_) { o.active_ = false; }
    void dismiss() noexcept { active_ = false; }
private:
    F func_;
    bool active_;
};

template <typename F> ScopeExit<F> make_scope_exit(F f) noexcept { return ScopeExit<F>(std::move(f)); }
```

用法：

```cpp
auto guard = make_scope_exit([&]{ hal_unlock_resource(); });
// ... 中间有多个 return
// 若成功并想取消 cleanup:
guard.dismiss();
```

这在没有异常支持时仍然非常有用：任何 `return` 都会触发 lambda，确保资源被清理。

## 最后

把 RAII 用好，就是把“麻烦的清理工作”交给 C++ 的析构魔法师去做——你只需要专注于写业务逻辑，不用每天像程序员版的保洁员那样记着“这根管线我什么时候关”。不过要记住：请善待析构函数，不要让它成为阻塞地狱的起点。把析构写成一个温柔而高效的护士：安静、快速、不会在别人睡觉时把心脏按坏。

