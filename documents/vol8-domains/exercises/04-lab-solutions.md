---
title: "卷八 Lab 实验参考"
description: "卷八 Lab（从串口帧到网络控制台）的实验参考：六个步骤加 L5 挑战的逐步解答，每步标注知识点链接，所有输出在 WSL Arch（g++ 16.1.1）真实运行得到。网络步骤用回环地址实测，服务器进程测试后已清理。"
chapter: 8
order: 4
tags:
  - host
  - intermediate
  - cpp-modern
  - 嵌入式
  - 网络编程
  - 状态机
  - 循环缓冲区
  - 寄存器
  - 异步编程
difficulty: intermediate
platform: host
cpp_standard: [17, 20, 23]
reading_time_minutes: 35
prerequisites: []
related: []
---

# 卷八 Lab 实验参考

> 所有输出在 WSL Arch（g++ 16.1.1）真实运行得到。网络步骤（步骤 6）用 `127.0.0.1` 回环实测，服务器进程验证后已终止清理。建议卡住时先看「思路」逐步对照。

## 步骤 1：寄存器位运算工具箱 {#lab-1}

**思路**：提取字段 = 先右移把目标位段挪到低位、再掩码切掉无关位；写入字段 = 先清位（`& ~(mask << offset)`）再或入左移好的值。先后顺序反了要么切错位段、要么把旧值带进去。

1. `field = (reg >> 8) & 0xF`：`0x4500 >> 8 = 0x45`，掩码后 `0x5`——切出来的正是 offset 8 的 4 位。→ 知识点：[类型安全的寄存器访问](../embedded/02-type-safe-register-access.md)（`reg_field` 的 `mask` 与 `read_raw`）
2. 置位 `reg |= (1u << 13)` → `0x6500`；清位 `reg &= ~(1u << 10)` → `0x6100`；翻转两次 → 回到 `0x6100`（异或的自反性）。→ 知识点：[第 11 篇](../embedded/01-led/06-hal-gpio-output.md)（`HAL_GPIO_WritePin` 底层的 ODR 位操作）
3. 字段写入 `reg = (reg & ~(0x3u << 14)) | (0b10u << 14)` → `0xA100`；读回 `(reg >> 14) & 0x3 == 0x2`。→ 知识点：[类型安全的寄存器访问](../embedded/02-type-safe-register-access.md)（`write_raw` 的 `(v & ~mask) | value` 结构）

**代码**：

```cpp
#include <cstdint>
#include <cstdio>

int main() {
    std::uint32_t reg = 0x00004500u;   // bits 8, 10, 14 high
    std::printf("reg            = 0x%08X\n", reg);

    constexpr std::uint32_t kMask4 = 0xFu;
    std::uint32_t field = (reg >> 8) & kMask4;
    std::printf("field@8        = 0x%X\n", field);

    reg |= (1u << 13);
    std::printf("set  bit13     = 0x%08X\n", reg);

    reg &= ~(1u << 10);
    std::printf("clr  bit10     = 0x%08X\n", reg);

    reg ^= (1u << 11);
    reg ^= (1u << 11);
    std::printf("tgl  bit11 x2  = 0x%08X\n", reg);

    constexpr std::uint32_t kMask2 = 0x3u, kOffset14 = 14u;
    reg = (reg & ~(kMask2 << kOffset14)) | (0b10u << kOffset14);
    std::printf("field@14 := 2  = 0x%08X\n", reg);

    std::printf("field@14 read  = 0x%X\n", (reg >> kOffset14) & kMask2);
    return 0;
}
```

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra reg_toolbox.cpp -o reg_toolbox && ./reg_toolbox
reg            = 0x00004500
field@8        = 0x5
set  bit13     = 0x00006500
clr  bit10     = 0x00006100
tgl  bit11 x2  = 0x00006100
field@14 := 2  = 0x0000A100
field@14 read  = 0x2
```

## 步骤 2：UART 8N1 帧编解码 {#lab-2}

**思路**：编码是纯查表式的位展开；解码的关键是「bit 中心」的坐标——起始边沿迟到 2 个 tick，起始位中心在 $2+8$，第 i 个数据位中心在 $2+8+16\times (i+1)$，停止位中心再往后 16。

1. `0xA5 = 0b10100101`，LSB 在前 → `d0..d7 = 1,0,1,0,0,1,0,1`，帧 `0 10100101 1` = `0101001011`。→ 知识点：[第 32 篇](../embedded/03-uart/02-uart-protocol-basics.md)「数据帧解剖」「数据位（Data Bits）」两节
2. 16x 过采样：边沿迟到 2 tick 后，每个 bit 中心照常能取到——16 个采样点里中心 3 个做多数表决，把 ±1 tick 的抖动/噪声滤掉，这就是「只要偏差在 2~3% 以内，通信依然可靠」的机制。→ 知识点：[第 32 篇](../embedded/03-uart/02-uart-protocol-basics.md)「过采样：接收方如何找到 bit 的中心」一节
3. 吞吐：`1/115200 = 8.68 µs/bit`，8N1 帧 10 bit = `86.81 µs`，有效速率 `115200/10 = 11520 字节/秒`——与第 32 篇的数字（86.8 µs、11520 字节/秒）对上。→ 知识点：同上（「一个帧的传输时间是 10/115200」）
4. 停止位中心 3 个采样点全改 0 后多数表决为 0 → framing error；只改 1 个会被表决滤掉——「3 取 2」的容错边界。→ 知识点：同上（停止位的作用）

**代码**：

```cpp
#include <array>
#include <cstdint>
#include <cstdio>

static constexpr std::uint32_t kBaud = 115200;

std::array<std::uint8_t, 10> encode_8n1(std::uint8_t byte) {
    std::array<std::uint8_t, 10> f{};
    f[0] = 0;
    for (int i = 0; i < 8; ++i) f[1 + i] = (byte >> i) & 1u;
    f[9] = 1;
    return f;
}

struct DecodeResult { std::uint8_t value = 0; bool ok = false; bool framing_error = false; };
static DecodeResult decode(const std::array<std::uint8_t, 162>& s) {
    auto sample = [&](int c) {
        int ones = 0;
        for (int k = -1; k <= 1; ++k) ones += s[c + k];
        return ones >= 2 ? 1 : 0;
    };
    DecodeResult r{};
    int tick = 2 + 8;
    if (sample(tick) != 0) { r.framing_error = true; return r; }
    std::uint8_t v = 0;
    for (int i = 0; i < 8; ++i) {
        tick += 16;
        if (sample(tick)) v |= static_cast<std::uint8_t>(1u << i);
    }
    tick += 16;
    if (sample(tick) != 1) { r.framing_error = true; return r; }
    r.value = v; r.ok = true;
    return r;
}

int main() {
    const double bit_time_us = 1.0e6 / static_cast<double>(kBaud);
    const double frame_time_us = bit_time_us * 10.0;
    const double bytes_per_sec = static_cast<double>(kBaud) / 10.0;
    std::printf("115200 baud: 1 bit = %.2f us, 8N1 frame = %.2f us, %.0f bytes/s\n",
                bit_time_us, frame_time_us, bytes_per_sec);

    const std::uint8_t byte = 0xA5;   // 0b10100101
    auto f = encode_8n1(byte);
    std::printf("frame for 0x%02X: ", byte);
    for (int i = 0; i < 10; ++i) std::printf("%u", f[i]);
    std::printf("   (start d0..d7 stop)\n");

    std::array<std::uint8_t, 162> s{};
    int pos = 2;
    for (int b = 0; b < 10; ++b)
        for (int k = 0; k < 16; ++k) s[pos++] = f[b];

    auto r = decode(s);
    std::printf("decode: ok=%d value=0x%02X framing_error=%d\n", r.ok, r.value, r.framing_error);

    auto bad = s;
    bad[2 + 16 * 9 + 7] = 0; bad[2 + 16 * 9 + 8] = 0; bad[2 + 16 * 9 + 9] = 0;
    auto r2 = decode(bad);
    std::printf("corrupt-stop: ok=%d framing_error=%d\n", r2.ok, r2.framing_error);
    return 0;
}
```

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra uart8n1.cpp -o uart8n1 && ./uart8n1
115200 baud: 1 bit = 8.68 us, 8N1 frame = 86.81 us, 11520 bytes/s
frame for 0xA5: 0101001011   (start d0..d7 stop)
decode: ok=1 value=0xA5 framing_error=0
corrupt-stop: ok=0 framing_error=1
```

## 步骤 3：环形缓冲区 {#lab-3}

**思路**：head/tail 是单调递增的 `size_t`（回绕到 $2^{64}$ 才发生，等于永不发生），只在数组下标处 `& (N-1)`；「满时拒绝 + 计数」是 ISR 场景的标准策略——丢字节要**记账**，不然调试时连丢了几个都不知道。

1. 批次 1：容量 15（N=16 留一槽），push 20 → 接受 15、丢 5。→ 知识点：[第 37 篇](../embedded/03-uart/07-circular-buffer-lock-free-spsc.md)「空（empty）/ 满（full）」一节（留一槽区分空满）、Homework 8.1-C 的实测结论（教材 `next()` 方案的满判定缺陷与单调计数器修复）
2. 批次 2：pop 8 后 head=20、tail=8，再 push 8 个写入下标 4..11（`& 15` 环绕）——drain 出来 `8 9 10 11 12 13 14 100 ... 107`，顺序完全正确，checksum 905。→ 知识点：[第 37 篇](../embedded/03-uart/07-circular-buffer-lock-free-spsc.md)「2 的幂技巧：零开销环绕」一节
3. 「丢字节 + 计数」适合 ISR：中断上下文不能阻塞、不能慢慢处理，满了要么丢（记数）要么覆盖（更糟），计数至少让你事后知道发生了什么。→ 知识点：[第 37 篇](../embedded/03-uart/07-circular-buffer-lock-free-spsc.md)「N = 128 够不够？」一节

**代码**：

```cpp
#include <array>
#include <cstddef>
#include <cstdio>

template <std::size_t N>
class CircularBuffer {
    static_assert(N > 0 && (N & (N - 1)) == 0, "N must be a power of 2");
public:
    bool push(std::byte b) noexcept {
        if (full()) { ++dropped_; return false; }
        buf_[head_ & (N - 1)] = b;   // mask only on the array index
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
    bool full()  const noexcept { return head_ - tail_ == N - 1; }
    std::size_t size() const noexcept { return head_ - tail_; }
    std::size_t dropped() const noexcept { return dropped_; }
private:
    std::array<std::byte, N> buf_{};
    std::size_t head_ = 0, tail_ = 0, dropped_ = 0;
};

int main() {
    CircularBuffer<16> cb;
    std::printf("-- batch 1: push 20 bytes (0..19) --\n");
    for (int i = 0; i < 20; ++i) cb.push(static_cast<std::byte>(i));
    std::printf("size=%zu dropped=%zu (expected 15 / 5)\n", cb.size(), cb.dropped());
    std::printf("-- batch 2: pop 8, then push 8 more (wrap-around) --\n");
    std::byte b;
    std::printf("pop: ");
    for (int i = 0; i < 8; ++i) { cb.pop(b); std::printf("%u ", static_cast<unsigned>(b)); }
    std::printf("\n");
    for (int i = 100; i < 108; ++i) cb.push(static_cast<std::byte>(i));
    std::printf("size=%zu dropped=%zu\n", cb.size(), cb.dropped());
    unsigned long sum_out = 0;
    std::printf("drain: ");
    while (cb.pop(b)) { std::printf("%u ", static_cast<unsigned>(b)); sum_out += static_cast<unsigned>(b); }
    std::printf("\n");
    std::printf("empty=%d size=%zu dropped=%zu checksum=%lu\n",
                cb.empty(), cb.size(), cb.dropped(), sum_out);
    return 0;
}
```

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra ringbuf.cpp -o ringbuf && ./ringbuf
-- batch 1: push 20 bytes (0..19) --
size=15 dropped=5 (expected 15 / 5)
-- batch 2: pop 8, then push 8 more (wrap-around) --
pop: 0 1 2 3 4 5 6 7
size=15 dropped=5
drain: 8 9 10 11 12 13 14 100 101 102 103 104 105 106 107
empty=1 size=0 dropped=5 checksum=905
```

## 步骤 4：7 状态消抖状态机 {#lab-4}

**思路**：核心路径 4 个状态处理正常按下/释放，启动路径 3 个状态处理「上电时按钮已按住」的边界；DebouncingPress 里「先查反弹、再查回低、最后查超时」的顺序决定了：任何一次反弹都重置计时器、明确回低立即放弃、只有持续稳定才确认。

1. 场景 A：t=9 是最后一次反弹，t=30 时差值 21 ≥ 20 → Pressed；释放方向 t=84 之后稳定，t=110 差值 26 → Released。→ 知识点：[第 25 篇](../embedded/02-button/07-debounce-state-machine.md)「State::DebouncingPress / DebouncingRelease」两节
2. 场景 B：BootSync 采样为 1 → `boot_locked_` 置位进入 BootPressed；释放消抖完成后**静默**回 Idle、清锁，不触发 Released——用户从没在运行期「按下」过。→ 知识点：[第 25 篇](../embedded/02-button/07-debounce-state-machine.md)「Boot-lock 检查」「BootPressed 和 BootReleaseDebouncing」两节
3. 场景 C：DebouncingPress 中采样回到 0（情况 2），直接放弃回 Idle，不产生任何事件。→ 知识点：同上（情况 2：信号明确回到了低电平）

**代码**：

```cpp
#include <cstdint>
#include <cstdio>
#include <functional>
#include <type_traits>
#include <utility>
#include <variant>

struct Pressed {};
struct Released {};
using ButtonEvent = std::variant<Pressed, Released>;

class Button {
public:
    enum class State {
        BootSync, Idle, DebouncingPress, Pressed,
        DebouncingRelease, BootPressed, BootReleaseDebouncing
    };
    static constexpr std::uint32_t kDebounceMs = 20;

    void set_callback(std::function<void(ButtonEvent)> cb) { cb_ = std::move(cb); }

    void poll(std::uint8_t sample, std::uint32_t now) {
        switch (state_) {
        case State::BootSync:
            raw_ = sample; stable_ = sample; debounce_start_ = now;
            boot_locked_ = sample;
            state_ = sample ? State::BootPressed : State::Idle;
            return;
        case State::Idle:
            if (sample) { raw_ = true; debounce_start_ = now; state_ = State::DebouncingPress; }
            return;
        case State::DebouncingPress:
            if (sample != raw_) { raw_ = sample; debounce_start_ = now; }
            if (!sample) { state_ = State::Idle; return; }
            if ((now - debounce_start_) < kDebounceMs) return;
            stable_ = true; state_ = State::Pressed;
            emit(Pressed{});
            return;
        case State::Pressed:
            if (sample != raw_) { raw_ = sample; debounce_start_ = now; state_ = State::DebouncingRelease; }
            return;
        case State::DebouncingRelease: {
            if (sample != raw_) {
                raw_ = sample; debounce_start_ = now;
                if (sample) state_ = State::Pressed;
                return;
            }
            if (sample) { state_ = State::Pressed; return; }
            if ((now - debounce_start_) < kDebounceMs) return;
            stable_ = false; state_ = State::Idle;
            if (boot_locked_) { boot_locked_ = false; return; }
            emit(Released{});
            return;
        }
        case State::BootPressed:
            if (sample != raw_) { raw_ = sample; debounce_start_ = now; state_ = State::BootReleaseDebouncing; }
            return;
        case State::BootReleaseDebouncing: {
            if (sample != raw_) {
                raw_ = sample; debounce_start_ = now;
                if (sample) state_ = State::BootPressed;
                return;
            }
            if (sample) { state_ = State::BootPressed; return; }
            if ((now - debounce_start_) < kDebounceMs) return;
            boot_locked_ = false; stable_ = false; state_ = State::Idle;
            return;   // silent unlock: no event
        }
        }
    }
private:
    void emit(ButtonEvent e) { if (cb_) cb_(e); }
    State state_ = State::BootSync;
    std::uint8_t raw_ = 0, stable_ = 0, boot_locked_ = 0;
    std::uint32_t debounce_start_ = 0;
    std::function<void(ButtonEvent)> cb_;
};

struct Step { std::uint32_t t; std::uint8_t s; };

int main() {
    const char* cur_name = "";
    int pressed = 0, released = 0;
    auto run_scenario = [&](const char* name, std::initializer_list<Step> trace) {
        Button b;
        pressed = released = 0;
        cur_name = name;
        b.set_callback([&](ButtonEvent e) {
            std::visit([&](auto&& x) {
                using T = std::decay_t<decltype(x)>;
                if constexpr (std::is_same_v<T, Pressed>) {
                    ++pressed; std::printf("[%s] event: Pressed\n", cur_name);
                } else {
                    ++released; std::printf("[%s] event: Released\n", cur_name);
                }
            }, e);
        });
        for (const Step& st : trace) b.poll(st.s, st.t);
        std::printf("[%s] summary: pressed=%d released=%d\n", cur_name, pressed, released);
    };

    run_scenario("A", {{0,0},{1,1},{3,0},{5,1},{7,0},{9,1},{30,1},{80,0},{82,1},{84,0},{110,0}});
    run_scenario("B", {{0,1},{5,1},{30,1},{60,0},{62,1},{64,0},{90,0}});
    run_scenario("C", {{0,0},{5,1},{10,0},{50,0}});
    return 0;
}
```

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra button_sm.cpp -o button_sm && ./button_sm
[A] event: Pressed
[A] event: Released
[A] summary: pressed=1 released=1
[B] summary: pressed=0 released=0
[C] summary: pressed=0 released=0
```

## 步骤 5：命令处理器 {#lab-5}

**思路**：行拼装的全部状态就是一个固定缓冲 + 一个长度；`string_view` 只存「指针 + 长度」，比较与打印都不构造 `std::string`——零拷贝。**先 NUL 终止再分发**是血泪教训：不终止的话 `%s` 会一路读到前面行的残留字符——本 Lab 验证时真实出现过 `'TURBOSF'` 这种怪输出，逐字符拆开是 `TURBO` + 上一行 `STATUS` 残留的 `S` + 上两行 `LED OFF` 残留的 `F`：缓冲既没清空又没终止，`%s` 就从行尾一直读穿到最近的 `\0` 才停。

1. `feed` 的 `\n` 分支：`line_[len_] = '\0'` → `dispatch(string_view(line_.data(), len_))` → `len_ = 0`。→ 知识点：[第 42 篇](../embedded/03-uart/12-command-processor-and-main-walkthrough.md)「任务二：UART 接收 → 命令解析」「std::string_view 的零拷贝优势」两节
2. 会话验证：`HELP` 打印命令表、`LED ON/OFF` 驱动状态、`STATUS` 读回、`TURBO` 报未知、空行被忽略——输出与题面预期逐行一致。→ 知识点：[第 42 篇](../embedded/03-uart/12-command-processor-and-main-walkthrough.md)「handle_command：一个微型 shell」一节
3. 超长行防护：`len_ < line_.size() - 1` 保证至少留一格给 `\0`——和第 42 篇主循环里 `line_len < line_buf.size() - 1` 是同一行代码。→ 知识点：同上

**代码**：

```cpp
#include <array>
#include <cstdio>
#include <string_view>

struct Led {
    bool on = false;
    void turn(bool v) { on = v; }
};

class CommandShell {
public:
    explicit CommandShell(Led& led) : led_(led) {}
    void feed(char c) {
        if (c == '\r') return;
        if (c == '\n') {
            if (len_ > 0) {
                line_[len_] = '\0';          // NUL-terminate BEFORE dispatch
                dispatch(std::string_view(line_.data(), len_));
                len_ = 0;
            }
            return;
        }
        if (len_ < line_.size() - 1) line_[len_++] = c;
    }
private:
    void dispatch(std::string_view cmd) {
        if (cmd == "LED ON")  { led_.turn(true);  std::printf("OK: LED ON\n"); }
        else if (cmd == "LED OFF") { led_.turn(false); std::printf("OK: LED OFF\n"); }
        else if (cmd == "STATUS")  { std::printf("STATUS: led=%s\n", led_.on ? "on" : "off"); }
        else if (cmd == "HELP")    { std::printf("Commands: LED ON, LED OFF, STATUS, HELP\n"); }
        else if (cmd.empty())      { /* ignore */ }
        else                       { std::printf("ERR: unknown command '%.*s'\n",
                                                static_cast<int>(cmd.size()), cmd.data()); }
    }
    std::array<char, 32> line_{};
    std::size_t len_ = 0;
    Led& led_;
};

int main() {
    Led led;
    CommandShell shell(led);
    const char* session =
        "HELP\r\nLED ON\r\nSTATUS\r\nLED OFF\r\nSTATUS\r\nTURBO\r\n\r\n";
    for (const char* p = session; *p; ++p) shell.feed(*p);
    return 0;
}
```

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra cmdproc.cpp -o cmdproc && ./cmdproc
Commands: LED ON, LED OFF, STATUS, HELP
OK: LED ON
STATUS: led=on
OK: LED OFF
STATUS: led=off
ERR: unknown command 'TURBO'
```

## 步骤 6：网络控制台 {#lab-6}

**思路**：Reactor 的最小形态——一个 epoll 实例同时挂监听 fd 与连接 fd，`epoll_wait` 醒来按 fd 分派；LT 模式下事件循环不用循环读到 EAGAIN 也能工作（没读完下次还通知），但正确姿势不变。优雅关闭靠「200ms 超时 + 信号置标志」：`epoll_wait` 不会永远睡死。一个隐患先说在前面：参考代码的连接 fd 只注册了 `EPOLLIN`，回写里 `write` 一旦返回 EAGAIN（`w <= 0` 且非 EINTR 的分支）就 break——**未写完的字节被静默丢弃**；回环上收发缓冲充足、几乎不触发 EAGAIN，所以这个坑在回环验收里测不出来，只有真实网络拥塞时才以「丢字节还不出错」的形式暴露。生产写法：把未写完的部分缓存起来、注册 `EPOLLOUT` 等可写再重发（或至少把丢弃的字节数计下来打日志）。

1. 服务器：`UniqueFd` 管 fd 生命周期（任意路径退出都自动 close）；`SIGPIPE` 忽略；回写处理短写与 EINTR；连接断开从 epoll 注销并计数。→ 知识点：[00 · 传统 socket 编程](../networking/00-traditional-socket-basics.md)（五步 + SIGPIPE/SO_REUSEADDR）、[01 · 现代 socket 封装](../networking/01-modern-socket-wrapping.md)（RAII 收掉裸 fd）
2. 事件循环：`epoll_wait` 每次醒来遍历就绪 fd——监听 fd 走 accept 分支、连接 fd 走读写分支，这就是 [03 · Reactor 模式](../networking/03-reactor-pattern.md) 四角色里的 Demultiplexer + Dispatcher。→ 知识点：[02 · epoll](../networking/02-epoll-io-multiplexing.md)（三个 API）、[03 · Reactor 模式](../networking/03-reactor-pattern.md)
3. 客户端 64KB burst 全量回显、逐字节一致——大 burst 是对抗性验收，4KB 小消息根本测不出短写/丢字节的问题。→ 知识点：[02 · epoll](../networking/02-epoll-io-multiplexing.md)「这就是『别被测试骗了』的典型」

**代码**（服务器核心）：

```cpp
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

static constexpr std::uint16_t kPort = 23041;
static constexpr std::size_t kMaxConns = 64;

struct Conn {
    UniqueFd fd;
    std::array<char, 4096> buf{};
    std::size_t pending = 0;
    bool closed = false;
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
    ::listen(lfd.get(), 32);
    ::fcntl(lfd.get(), F_SETFL, ::fcntl(lfd.get(), F_GETFL, 0) | O_NONBLOCK);
    std::printf("console server listening on 0.0.0.0:%u\n", kPort);
    std::fflush(stdout);

    UniqueFd ep(::epoll_create1(0));
    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = lfd.get();
    ::epoll_ctl(ep.get(), EPOLL_CTL_ADD, lfd.get(), &ev);

    Conn conns[kMaxConns];
    std::size_t conn_count = 0;

    while (!g_stop) {
        epoll_event evs[16];
        int n = ::epoll_wait(ep.get(), evs, 16, 200);   // 200ms tick to notice g_stop
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
                        std::printf("client connected (fd=%d), total=%zu\n", c, conn_count);
                        std::fflush(stdout);
                    } else {
                        ::close(c);
                    }
                }
            } else {
                Conn* conn = nullptr;
                for (std::size_t j = 0; j < conn_count; ++j)
                    if (conns[j].fd.get() == fd) { conn = &conns[j]; break; }
                if (!conn || conn->closed) continue;
                for (;;) {
                    ssize_t r = ::read(fd, conn->buf.data(), conn->buf.size());
                    if (r > 0) {
                        std::size_t off = 0;
                        while (off < static_cast<std::size_t>(r)) {
                            ssize_t w = ::write(fd, conn->buf.data() + off,
                                                static_cast<std::size_t>(r) - off);
                            if (w < 0 && errno == EINTR) continue;
                            if (w <= 0) break;
                            off += static_cast<std::size_t>(w);
                        }
                        conn->pending += static_cast<std::size_t>(r);
                        continue;
                    }
                    if (r == 0) {
                        ::epoll_ctl(ep.get(), EPOLL_CTL_DEL, fd, nullptr);
                        conn->closed = true;
                        conn->fd.reset();
                        std::printf("client disconnected (fd=%d), echoed=%zu bytes\n",
                                    fd, conn->pending);
                        std::fflush(stdout);
                    } else if (errno == EAGAIN) {
                        break;
                    }
                    break;
                }
            }
        }
    }
    std::printf("server shutting down, %zu connections served\n", conn_count);
    return 0;
}
```

客户端连 `127.0.0.1:23041`，发 `HELP\r\n` 校验回显、再发 64KB burst 断言全量回显（客户端代码与题面描述一致，此处从略）。

**验证输出**（服务器后台启动 → 客户端 → `kill -TERM`）：

```text
$ ./console_server &
console server listening on 0.0.0.0:23041
client connected (fd=6), total=1
[command] echoed 6 bytes: OK
[burst] echoed 65536/65536 bytes: OK (all 64KB)
client disconnected (fd=6), echoed=65542 bytes
$ kill -TERM %1
server shutting down, 1 connections served
```

## 附加挑战（L5）：无锁 SPSC 环形缓冲区，TSan 验收 {#lab-l5}

**思路**：SPSC 无锁的前提是「head 只有生产者写、tail 只有消费者写」；锁被换成了 acquire/release——release 保证「数据写入」先于「索引发布」，acquire 保证「索引可见」时「数据必然可见」。这就是无锁编程里内存序的全部戏法。

1. `push`：relaxed 读自己的 head（只有自己写，无需同步）；acquire 读 tail——确保看到消费者发布的最新 tail（及其之前的所有出队），判满才可靠；release 写 head——确保 `buf_[i] = v` 不会被编译器/CPU 重排到 head 发布之后。`pop` 对称。→ 知识点：[第 37 篇](../embedded/03-uart/07-circular-buffer-lock-free-spsc.md)（SPSC 分工）、[中断安全的代码编写](../embedded/05-interrupt-safe-coding.md)（共享数据跨执行流的可见性）
2. 全 relaxed 的后果：写入可能被重排到索引发布之后，消费者 acquire 到新 head 却读到旧数据；甚至 head/tail 被缓存进寄存器、双线程互相看不见进度——TSan 会把这类问题报成 data race。→ 知识点：同上
3. 验收：500 万条严格递增校验 + checksum $\frac{5000000×5000001}{2} = 12500002500000$ 精确命中；TSan 构建零报告。→ 知识点：同上

**代码**：

```cpp
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <thread>

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
    constexpr std::uint32_t kTotal = 5'000'000;
    SpscRing<256> ring;
    std::uint64_t sum = 0;
    std::size_t count = 0;

    std::thread producer([&] {
        for (std::uint32_t i = 1; i <= kTotal; ++i)
            while (!ring.push(i)) {}   // spin while full
    });
    std::thread consumer([&] {
        std::uint32_t prev = 0;
        std::uint32_t v;
        while (count < kTotal) {
            if (ring.pop(v)) {
                if (v != prev + 1) {
                    std::printf("ORDER VIOLATION: got %u after %u\n", v, prev);
                    std::abort();
                }
                prev = v;
                sum += v;
                ++count;
            }
        }
    });
    producer.join();
    consumer.join();

    const std::uint64_t expected = static_cast<std::uint64_t>(kTotal) * (kTotal + 1) / 2;
    std::printf("count=%zu checksum=%llu expected=%llu match=%d\n",
                count, static_cast<unsigned long long>(sum),
                static_cast<unsigned long long>(expected), sum == expected ? 1 : 0);
    return 0;
}
```

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra -O2 -pthread spsc_l5.cpp -o spsc_l5 && ./spsc_l5
count=5000000 checksum=12500002500000 expected=12500002500000 match=1
$ g++ -std=c++20 -Wall -Wextra -g -O1 -fsanitize=thread spsc_l5.cpp -o spsc_l5_tsan && ./spsc_l5_tsan
count=5000000 checksum=12500002500000 expected=12500002500000 match=1        ← TSan 零报告
```
