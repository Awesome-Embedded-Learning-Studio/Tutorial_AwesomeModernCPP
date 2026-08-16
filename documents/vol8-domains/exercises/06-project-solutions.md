---
title: "卷八 Project 参考实现"
description: "卷八综合项目（MiniDevice 主机版智能设备模拟器）的完整参考实现：四层任务逐步讲解，每步标注知识点链接，含核心设备、8N1 帧层与 Tensor 传感器、epoll 网络控制台与对抗验收、弱引用观察者、双线程无锁流水线的真实运行输出（WSL Arch，g++ 16.1.1）。"
chapter: 8
order: 6
tags:
  - host
  - advanced
  - cpp-modern
  - 嵌入式
  - 网络编程
  - 状态机
  - 无锁
  - 并发
  - 模板
difficulty: advanced
platform: host
cpp_standard: [17, 20, 23]
reading_time_minutes: 28
prerequisites: []
related: []
---

# 卷八 Project 参考实现

> 全部输出在 WSL Arch（g++ 16.1.1）真实运行得到，网络层用 `127.0.0.1` 回环实测，服务器进程验证后已清理。参考实现只是**一种**过关方式；你的实现不一样、验收标准对得上，就都是对的。环形缓冲区沿用 Homework 8.1-C 实测验证过的「单调计数器」方案，与教材第 37 篇示例代码的差异在那里有完整说明。

## 核心任务（L2）：能跑起来的设备 {#pj-core}

**思路**：先立契约（L1 热身），再实现。`Led` 把「电路怎么接的」封进 `ActiveLevel`，对外只有逻辑语义；`Button` 是虚拟时钟驱动的消抖器；`Device` 把环形缓冲区、行拼装、命令分发拧成一个整体——每一块都能在主机上单独验证。

**L1 热身骨架**——声明先行，`-fsyntax-only` 零警告通过。→ 知识点：[第 16 篇：LED 模板](../embedded/01-led/11-cpp-led-template.md)（`ActiveLevel` 是应用层概念，与 HAL 无关）

```cpp
// device_skeleton.hpp（热身版：只有声明）
#pragma once
#include <cstdint>
#include <string_view>

class Led {
public:
    enum class ActiveLevel { Low, High };
    void on() noexcept;
    void off() noexcept;
    void toggle() noexcept;
    bool is_on() const noexcept;
private:
    bool logical_on_ = false;
    ActiveLevel level_ = ActiveLevel::Low;
};

class Button {
public:
    void poll(std::uint8_t sample, std::uint32_t now_ms) noexcept;
    bool stable_pressed() const noexcept;
private:
    static constexpr std::uint32_t kDebounceMs = 20;
    std::uint8_t last_raw_ = 0;
    std::uint8_t stable_ = 0;
    std::uint32_t last_change_ = 0;
};

void run_command(Led& led, Button& btn, std::string_view cmd);
```

编译命令为 `g++ -std=c++20 -Wall -Wextra -fsyntax-only skeleton_main.cpp`，本机实测零警告、退出码 0——热身过。

**`device.hpp`**——四件套：Led、Button、CircularBuffer、Device。

```cpp
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>

// ---- Led: active-level logic is compile-time configuration ----
class Led {
public:
    enum class ActiveLevel { Low, High };
    explicit Led(ActiveLevel level = ActiveLevel::Low) : level_(level) {}
    void on() noexcept { logical_on_ = true; }
    void off() noexcept { logical_on_ = false; }
    void toggle() noexcept { logical_on_ = !logical_on_; }
    bool is_on() const noexcept { return logical_on_; }
    // the pin level the hardware would actually see
    bool pin_level_for(bool logical) const noexcept {
        return level_ == ActiveLevel::Low ? !logical : logical;
    }
private:
    bool logical_on_ = false;
    ActiveLevel level_;
};
```

`on()` 就是「点亮」、`off()` 就是「熄灭」，低电平有效电路里引脚电平取反——这是第 16 篇 LED 模板 `on()/off()` 编译期分支的主机等价物。→ 知识点：[第 16 篇：LED 模板](../embedded/01-led/11-cpp-led-template.md)「on()和off()：编译时的电平分支」一节

```cpp
// ---- Button: non-blocking debounce on a virtual clock ----
class Button {
public:
    static constexpr std::uint32_t kDebounceMs = 20;
    bool poll(std::uint8_t sample, std::uint32_t now_ms) noexcept {
        bool changed = false;
        if (sample != last_raw_) {
            last_raw_ = sample;
            last_change_ = now_ms;
        }
        if ((now_ms - last_change_) >= kDebounceMs && last_raw_ != stable_) {
            stable_ = last_raw_;
            changed = true;
        }
        return changed;
    }
    bool stable_pressed() const noexcept { return stable_ != 0; }
private:
    std::uint8_t last_raw_ = 0;
    std::uint8_t stable_ = 0;
    std::uint32_t last_change_ = 0;
};
```

→ 知识点：[第 24 篇：非阻塞消抖](../embedded/02-button/06-non-blocking-debounce.md)「非阻塞消抖算法」「溢出的安全性」两节

```cpp
// ---- CircularBuffer: power-of-2 SPSC ring (monotonic counters) ----
template <std::size_t N>
class CircularBuffer {
    static_assert(N > 0 && (N & (N - 1)) == 0, "N must be a power of 2");
public:
    bool push(std::byte b) noexcept {
        if (full()) { ++dropped_; return false; }
        buf_[head_ & (N - 1)] = b;
        ++head_;
        return true;
    }
    bool pop(std::byte& out) noexcept {
        if (empty()) return false;
        out = buf_[tail_ & (N - 1)];
        ++tail_;
        return true;
    }
    bool empty() const noexcept { return head_ == tail_; }
    bool full() const noexcept { return head_ - tail_ == N - 1; }
    std::size_t size() const noexcept { return head_ - tail_; }
    std::size_t dropped() const noexcept { return dropped_; }
private:
    std::array<std::byte, N> buf_{};
    std::size_t head_ = 0, tail_ = 0, dropped_ = 0;
};
```

→ 知识点：[第 37 篇：无锁环形缓冲区](../embedded/03-uart/07-circular-buffer-lock-free-spsc.md)（留一槽区分空满）、Homework 8.1-C（满判定修复的完整论证）

```cpp
// ---- Device: the simulated gadget ----
class Device {
public:
    Led led{Led::ActiveLevel::Low};
    Button button;
    CircularBuffer<64> rx;

    void feed(char c) {
        if (c == '\r') return;
        if (c == '\n') {
            if (line_len_ > 0) {
                line_[line_len_] = '\0';
                run_command(std::string_view(line_.data(), line_len_));
                line_len_ = 0;
            }
            return;
        }
        if (line_len_ < line_.size() - 1) line_[line_len_++] = c;
    }

    void run_command(std::string_view cmd) {
        if (cmd == "LED ON") { led.on(); std::printf("OK: LED ON\n"); }
        else if (cmd == "LED OFF") { led.off(); std::printf("OK: LED OFF\n"); }
        else if (cmd == "LED TOGGLE") { led.toggle(); std::printf("OK: LED TOGGLED\n"); }
        else if (cmd == "BTN") { simulate_button_press(); }
        else if (cmd == "STATUS") {
            std::printf("STATUS: led=%s btn=%s pin_level=%u\n",
                        led.is_on() ? "on" : "off",
                        button.stable_pressed() ? "pressed" : "released",
                        led.pin_level_for(led.is_on()) ? 1u : 0u);
        } else if (cmd == "HELP") {
            std::printf("Commands: LED ON, LED OFF, LED TOGGLE, BTN, STATUS, HELP\n");
        } else if (cmd.empty()) {
            /* ignore empty lines */
        } else {
            std::printf("ERR: unknown command '%.*s'\n",
                        static_cast<int>(cmd.size()), cmd.data());
        }
    }

private:
    void simulate_button_press() {
        const std::uint32_t kNow = now_ms_;
        std::uint32_t t = kNow;
        if (button.poll(1, t += 1)) std::printf("BTN: pressed\n");
        button.poll(0, t += 2);
        button.poll(1, t += 2);
        button.poll(0, t += 2);
        button.poll(1, t += 2);
        if (button.poll(1, t += 25)) std::printf("BTN: pressed\n");
        button.poll(0, t += 40);
        button.poll(1, t += 1);
        button.poll(0, t += 1);
        if (button.poll(0, t += 25)) std::printf("BTN: released\n");
        now_ms_ = t;
    }

    std::array<char, 64> line_{};
    std::size_t line_len_ = 0;
    std::uint32_t now_ms_ = 1000;
};
```

`feed` 就是第 42 篇主循环「pop 一个字节处理一个」的那段，`run_command` 就是 `handle_command` 的主机版；`simulate_button_press` 用虚拟时钟喂一段带反弹的采样——真实板子上这段信号来自 GPIO 读引脚，这里来自函数。→ 知识点：[第 42 篇：命令处理器](../embedded/03-uart/12-command-processor-and-main-walkthrough.md)「任务二」「handle_command」两节

**`main.cpp`**——stdin 驱动：

```cpp
#include "device.hpp"
#include <cstdio>

int main() {
    Device dev;
    std::printf("MiniDevice ready. Commands: HELP / LED ON / LED OFF / LED TOGGLE / BTN / STATUS / QUIT\n");
    char buf[128];
    while (std::fgets(buf, sizeof(buf), stdin) != nullptr) {
        std::string_view line(buf);
        if (line.size() > 0 && line.back() == '\n') line.remove_suffix(1);
        if (line == "QUIT") break;
        for (char c : line) dev.feed(c);
        dev.feed('\n');
    }
    std::printf("MiniDevice shut down\n");
    return 0;
}
```

**验证输出**（会话 `HELP / LED ON / STATUS / BTN / STATUS / LED TOGGLE / STATUS / LED OFF / QUIT`）：

```text
$ g++ -std=c++20 -Wall -Wextra main.cpp -o minidevice && ./minidevice < session.txt
MiniDevice ready. Commands: HELP / LED ON / LED OFF / LED TOGGLE / BTN / STATUS / QUIT
Commands: LED ON, LED OFF, LED TOGGLE, BTN, STATUS, HELP
OK: LED ON
STATUS: led=on btn=released pin_level=0
BTN: pressed
BTN: released
STATUS: led=on btn=released pin_level=0
OK: LED TOGGLED
STATUS: led=off btn=released pin_level=1
OK: LED OFF
MiniDevice shut down
```

注意 `pin_level` 的变化：led 逻辑「on」时低电平有效电路引脚是 0，逻辑「off」时是 1——`ActiveLevel::Low` 的换算在 STATUS 里是可见的。ASan/UBSan 构建跑同一会话零报告（输出同上）。

## 进阶任务（L3）：帧层与传感器 {#pj-adv}

**思路**：帧层是第 32 篇协议的主机实现，传感器是 Stage 1 Tensor 的实战使用；两者都作为「外设」挂进命令表，`Device` 核心一行不改——这就是分层的好处。

**8N1 帧层**：`encode_8n1` 逐位展开，`decode_8n1` 校验起止位。→ 知识点：[第 32 篇：UART 协议详解](../embedded/03-uart/02-uart-protocol-basics.md)「数据帧解剖」一节

```cpp
static std::array<std::uint8_t, 10> encode_8n1(std::uint8_t byte) {
    std::array<std::uint8_t, 10> f{};
    f[0] = 0;
    for (int i = 0; i < 8; ++i) f[1 + i] = (byte >> i) & 1u;
    f[9] = 1;
    return f;
}
static std::string decode_8n1(const std::string& bits, std::uint8_t& out) {
    if (bits.size() != 10) return "ERR: need exactly 10 bits";
    if (bits[0] != '0') return "ERR: start bit must be 0";
    if (bits[9] != '1') return "ERR: stop bit must be 1";
    std::uint8_t v = 0;
    for (int i = 0; i < 8; ++i)
        if (bits[1 + i] == '1') v |= static_cast<std::uint8_t>(1u << i);
    out = v;
    return "";
}
```

**Tensor 传感器**：`Tensor<1, 3>` 行主序 + `std::array` 存储，`SHOW` 直接遍历 `storage()` 找最大值——Stage 1 的全部要点。→ 知识点：[固定维度 Tensor](../ai/tiny_ml/stage1/06-tensor.md)、[行主序](../ai/tiny_ml/stage1/04-row-major.md)

```cpp
template <std::size_t Rows, std::size_t Cols, typename S = float>
class Tensor {
public:
    constexpr Tensor() = default;
    constexpr S& operator()(std::size_t i, std::size_t j) noexcept { return data_[i * Cols + j]; }
    constexpr const S& operator()(std::size_t i, std::size_t j) const noexcept { return data_[i * Cols + j]; }
    static constexpr std::size_t size() noexcept { return Rows * Cols; }
    constexpr const std::array<S, Rows * Cols>& storage() const noexcept { return data_; }
private:
    std::array<S, Rows * Cols> data_{};
};
```

命令分支挂进 main 的输入循环（`FRAME`/`DEFRAME`/`SENSE`/`SHOW`，完整代码见层 2 源文件，结构与核心层一致）。

**验证输出**（会话 `FRAME 41 / FRAME A5 / DEFRAME 0100000101 / DEFRAME 1111111111 / SENSE 25.5 60 420 / SHOW / LED ON / STATUS / QUIT`）：

```text
$ g++ -std=c++20 -Wall -Wextra adv_main.cpp -o minidevice2 && ./minidevice2 < session2.txt
MiniDevice v2 ready. Extra: FRAME <hex>, DEFRAME <10bits>, SENSE t h l, SHOW
frame(0x41) = 0100000101
frame(0xA5) = 0101001011
decoded 0x41
ERR: start bit must be 0
SENSE: stored (25.5, 60.0, 420.0)
SHOW flat[0..2] = [25.5 60.0 420.0] size=3
SHOW max = 420.0 at col 2
OK: LED ON
STATUS: led=on btn=released pin_level=0
MiniDevice v2 shut down
```

`DEFRAME 1111111111` 起始位不是 0，直接拒收；`SHOW` 的最大值 420.0 来自对 `storage()` 扁平数组的遍历——行主序与扁平存储一一对应的活例子。ASan/UBSan 构建零报告。

## 再进阶任务（L4）：网络控制台、对抗验收与弱引用观察者 {#pj-l4}

**思路**：网络控制台是 Lab 步骤 6 的延伸——每个连接一个独立 `Device` 状态；对抗验收用 8 线程并发 burst；弱引用观察者是第 4 篇 Chrome-like WeakPtr 的实战落地。一个隐患先记下（与 Lab 步骤 6 参考代码相同）：`net_server` 的连接 fd 只注册 `EPOLLIN`，回写遇 EAGAIN（`w <= 0` 且非 EINTR 分支）会 break，**未写完的字节被静默丢弃**——回环上几乎不触发 EAGAIN，8 客户端全绿也测不出它，只有真实网络拥塞时才暴露。生产写法：缓存未写完字节 + 注册 `EPOLLOUT` 重发（或至少把丢弃字节计数打出来）。

**`net_server.cpp`**——单线程 epoll（LT），`UniqueFd` + `accept4(SOCK_NONBLOCK)` + 200ms 超时轮询停止标志。→ 知识点：[00](../networking/00-traditional-socket-basics.md)、[01](../networking/01-modern-socket-wrapping.md)、[02](../networking/02-epoll-io-multiplexing.md)、[03](../networking/03-reactor-pattern.md)（四篇的地基全用上）

```cpp
#include "device.hpp"
#include <array>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {
volatile sig_atomic_t g_stop = 0;
void on_sig(int) { g_stop = 1; }

class UniqueFd {
public:
    UniqueFd() = default;
    explicit UniqueFd(int fd) : fd_(fd) {}
    ~UniqueFd() { reset(); }
    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;
    UniqueFd(UniqueFd&& o) noexcept : fd_(o.fd_) { o.fd_ = -1; }
    UniqueFd& operator=(UniqueFd&& o) noexcept {
        if (this != &o) { reset(); fd_ = o.fd_; o.fd_ = -1; }
        return *this;
    }
    void reset() { if (fd_ >= 0) { ::close(fd_); fd_ = -1; } }
    int get() const { return fd_; }
private:
    int fd_ = -1;
};
} // namespace

static constexpr std::uint16_t kPort = 23053;
static constexpr std::size_t kMaxConns = 128;

struct Conn {
    UniqueFd fd;
    Device dev;                          // per-connection device state
    std::array<char, 512> rbuf{};
};

int main() {
    std::signal(SIGPIPE, SIG_IGN);
    std::signal(SIGTERM, on_sig);
    std::signal(SIGINT, on_sig);

    UniqueFd lfd(::socket(AF_INET, SOCK_STREAM, 0));
    int yes = 1;
    ::setsockopt(lfd.get(), SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(kPort);
    ::bind(lfd.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    ::listen(lfd.get(), 64);
    ::fcntl(lfd.get(), F_SETFL, ::fcntl(lfd.get(), F_GETFL, 0) | O_NONBLOCK);
    std::printf("net console listening on 0.0.0.0:%u\n", kPort);
    std::fflush(stdout);

    UniqueFd ep(::epoll_create1(0));
    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = lfd.get();
    ::epoll_ctl(ep.get(), EPOLL_CTL_ADD, lfd.get(), &ev);

    Conn conns[kMaxConns];
    std::size_t conn_count = 0;
    std::size_t served = 0;

    while (!g_stop) {
        epoll_event evs[32];
        int n = ::epoll_wait(ep.get(), evs, 32, 200);
        if (n < 0) { if (errno == EINTR) continue; break; }
        for (int i = 0; i < n; ++i) {
            int fd = evs[i].data.fd;
            if (fd == lfd.get()) {
                for (;;) {
                    int c = ::accept4(lfd.get(), nullptr, nullptr, SOCK_NONBLOCK);
                    if (c < 0) break;
                    if (conn_count < kMaxConns) {
                        conns[conn_count].fd = UniqueFd(c);
                        ev.events = EPOLLIN;
                        ev.data.fd = c;
                        ::epoll_ctl(ep.get(), EPOLL_CTL_ADD, c, &ev);
                        ++conn_count;
                    } else {
                        ::close(c);
                    }
                }
            } else {
                Conn* conn = nullptr;
                for (std::size_t j = 0; j < conn_count; ++j)
                    if (conns[j].fd.get() == fd) { conn = &conns[j]; break; }
                if (!conn) continue;
                for (;;) {
                    ssize_t r = ::read(fd, conn->rbuf.data(), conn->rbuf.size());
                    if (r > 0) {
                        std::size_t off = 0;
                        while (off < static_cast<std::size_t>(r)) {
                            ssize_t w = ::write(fd, conn->rbuf.data() + off,
                                                static_cast<std::size_t>(r) - off);
                            if (w < 0 && errno == EINTR) continue;
                            if (w <= 0) break;
                            off += static_cast<std::size_t>(w);
                        }
                        continue;
                    }
                    if (r == 0) {
                        ::epoll_ctl(ep.get(), EPOLL_CTL_DEL, fd, nullptr);
                        conn->fd.reset();
                        ++served;
                    } else if (errno == EAGAIN) {
                        break;
                    }
                    break;
                }
            }
        }
    }
    std::printf("net console shutting down: %zu clients served\n", served);
    return 0;
}
```

**对抗验收客户端**：8 线程各发 8KB burst、逐字节比对。**弱引用观察者**：Chrome-like `WeakFlag`（原子引用计数控制块）+ `WeakPtrFactory`，设备销毁后看门狗回调 `get()` 安全返回空。→ 知识点：[Chrome-like WeakPtr](../cpp-deep-dives/pointer-semantics/04-chrome-weakptr.md)「WeakFlag」「为什么 control block 要引用计数」两节

```cpp
class WeakFlag {
public:
    void add_ref() { ref_count_.fetch_add(1, std::memory_order_relaxed); }
    void release() {
        if (ref_count_.fetch_sub(1, std::memory_order_acq_rel) == 1) delete this;
    }
    void invalidate() { is_valid_.store(false, std::memory_order_release); }
    bool is_valid() const { return is_valid_.load(std::memory_order_acquire); }
private:
    std::atomic<bool> is_valid_{true};
    std::atomic<int> ref_count_{1};
    ~WeakFlag() = default;
};

template <typename T> class WeakPtr {
public:
    WeakPtr() = default;
    WeakPtr(T* p, WeakFlag* f) : ptr_(p), flag_(f) { if (flag_) flag_->add_ref(); }
    WeakPtr(const WeakPtr& o) : ptr_(o.ptr_), flag_(o.flag_) { if (flag_) flag_->add_ref(); }
    WeakPtr& operator=(const WeakPtr& o) {
        if (this != &o) {
            if (flag_) flag_->release();
            ptr_ = o.ptr_; flag_ = o.flag_;
            if (flag_) flag_->add_ref();
        }
        return *this;
    }
    WeakPtr(WeakPtr&& o) noexcept : ptr_(o.ptr_), flag_(o.flag_) { o.ptr_ = nullptr; o.flag_ = nullptr; }
    ~WeakPtr() { if (flag_) flag_->release(); }
    bool is_valid() const { return flag_ && flag_->is_valid(); }
    T* get() const { return is_valid() ? ptr_ : nullptr; }
private:
    T* ptr_ = nullptr;
    WeakFlag* flag_ = nullptr;
};

template <typename T> class WeakPtrFactory {
public:
    explicit WeakPtrFactory(T* owner) : owner_(owner), flag_(new WeakFlag()) {}
    WeakPtrFactory(const WeakPtrFactory&) = delete;
    WeakPtrFactory& operator=(const WeakPtrFactory&) = delete;
    WeakPtr<T> get_weak_ptr() { return WeakPtr<T>(owner_, flag_); }
    ~WeakPtrFactory() {
        flag_->invalidate();
        flag_->release();
        flag_ = nullptr;
    }
private:
    T* owner_;
    WeakFlag* flag_;
};

struct Device {
    int id = 7;
    WeakPtrFactory<Device> factory{this};
    WeakPtr<Device> weak() { return factory.get_weak_ptr(); }
};

static void fire_watchdog(const char* name, WeakPtr<Device> w) {
    if (auto* d = w.get()) std::printf("[%s] device %d alive, watchdog acts\n", name, d->id);
    else std::printf("[%s] device gone, watchdog stays silent (no UB)\n", name);
}

int main() {
    WeakPtr<Device> w1, w2;
    {
        auto dev = std::make_unique<Device>();
        w1 = dev->weak();
        fire_watchdog("while alive", dev->weak());
        w2 = dev->weak();
        // dev destroyed here; the control block survives because w1/w2 hold refs
    }
    fire_watchdog("after destroy", w1);
    fire_watchdog("after destroy (copy)", w2);
    return 0;
}
```

**验证输出**（服务器后台 → 8 线程客户端 → 优雅退出；看门狗另跑）：

```text
$ ./net_server &
net console listening on 0.0.0.0:23053
[client 1] echoed 8192/8192 bytes: OK
[client 3] echoed 8192/8192 bytes: OK
[client 0] echoed 8192/8192 bytes: OK
[client 4] echoed 8192/8192 bytes: OK
[client 6] echoed 8192/8192 bytes: OK
[client 7] echoed 8192/8192 bytes: OK
[client 2] echoed 8192/8192 bytes: OK
[client 5] echoed 8192/8192 bytes: OK
all 8 concurrent clients done
$ kill -TERM %1
net console shutting down: 8 clients served

$ g++ -std=c++20 -Wall -Wextra watchdog.cpp -o watchdog && ./watchdog
[while alive] device 7 alive, watchdog acts
[after destroy] device gone, watchdog stays silent (no UB)
[after destroy (copy)] device gone, watchdog stays silent (no UB)
$ g++ -std=c++20 -Wall -Wextra -fsanitize=address,undefined -g watchdog.cpp -o watchdog_san && ./watchdog_san
（输出同上，ASan/UBSan 零报告）
```

8 个并发客户端全 OK、服务器优雅退出、看门狗在设备销毁后两次回调都安全静默——「8 并发 8KB」是这一层的对抗验收：换小消息测不出任何问题，只有并发 + burst 才压得出真相。

## 终极挑战（L5）：双线程无锁流水线，TSan 验收 {#pj-l5}

**思路**：这条流水线就是本卷数据链路的「并发终版」——设备线程（生产者）与网络线程（消费者）之间没有任何锁，全靠 SPSC 分工 + acquire/release 配对保证「数据先于索引可见」。

1. `push`：relaxed 读自己的 head（单写者无需同步）；**acquire** 读 tail——看到消费者发布的最新 tail 及它之前的所有出队，判满才不误判；**release** 写 head——保证槽位写入完成后才发布索引。`pop` 对称。→ 知识点：[第 37 篇](../embedded/03-uart/07-circular-buffer-lock-free-spsc.md)（SPSC 分工）、[中断安全的代码编写](../embedded/05-interrupt-safe-coding.md)（可见性与顺序）
2. 验收：100 万帧严格递增 + 生产者完成标志（release/acquire 配对读取），普通与 TSan 构建都通过、零报告。→ 知识点：同上

**代码**：

```cpp
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <thread>

// SPSC ring (Vyukov-style): device thread -> network thread
template <std::size_t N>
class SpscRing {
    static_assert(N > 1 && (N & (N - 1)) == 0, "N must be a power of 2");
public:
    bool push(std::uint32_t v) {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t tail = tail_.load(std::memory_order_acquire);
        if (head - tail >= N - 1) return false;
        buf_[head & (N - 1)] = v;
        head_.store(head + 1, std::memory_order_release);
        return true;
    }
    bool pop(std::uint32_t& v) {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        const std::size_t head = head_.load(std::memory_order_acquire);
        if (head == tail) return false;
        v = buf_[tail & (N - 1)];
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }
private:
    std::array<std::uint32_t, N> buf_{};
    alignas(64) std::atomic<std::size_t> head_{0};
    alignas(64) std::atomic<std::size_t> tail_{0};
};

int main() {
    constexpr std::uint32_t kFrames = 1'000'000;
    SpscRing<512> ring;
    std::atomic<bool> produced_all{false};

    std::thread device_thread([&] {        // producer: "device" encodes frames
        for (std::uint32_t i = 1; i <= kFrames; ++i)
            while (!ring.push(i)) {}
        produced_all.store(true, std::memory_order_release);
    });
    std::thread network_thread([&] {       // consumer: "network" ships them
        std::uint32_t v = 0, prev = 0;
        std::size_t count = 0;
        while (count < kFrames) {
            if (ring.pop(v)) {
                if (v != prev + 1) {
                    std::printf("ORDER VIOLATION: %u after %u\n", v, prev);
                    std::abort();
                }
                prev = v;
                ++count;
            }
        }
        std::printf("network thread shipped %zu frames\n", count);
    });
    device_thread.join();
    network_thread.join();
    std::printf("pipeline done: %u frames, producer done=%d\n",
                kFrames, produced_all.load(std::memory_order_acquire) ? 1 : 0);
    return 0;
}
```

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra -O2 -pthread pipeline.cpp -o pipeline && ./pipeline
network thread shipped 1000000 frames
pipeline done: 1000000 frames, producer done=1
$ g++ -std=c++20 -Wall -Wextra -g -O1 -fsanitize=thread pipeline.cpp -o pipeline_tsan && ./pipeline_tsan
network thread shipped 1000000 frames
pipeline done: 1000000 frames, producer done=1        ← TSan 零报告
```

到这里，这台主机上的设备就走完了本卷数据链路的全程：寄存器位运算 → 8N1 帧 → 环形缓冲区 → 消抖状态机 → 命令处理器 → epoll 网络控制台 → 无锁双线程流水线。每一层都能单独编译单独验证，合起来是一台完整的模拟设备——这就是「主机先行、硬件后置」的嵌入式开发节奏。
