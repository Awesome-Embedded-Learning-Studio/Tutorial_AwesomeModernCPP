#pragma once

#include <iostream>
#include <memory>
#include <string>

// 状态机模式(State Pattern)
//
// 本工程对应文章「状态机模式:从一坨 if/else 到类型安全的状态对象」。
// 这里实现文章第二步给出的面向对象 State 模式版本:
//   - State 是一个虚函数接口,每个具体状态自负其责地响应事件
//   - MediaPlayer 作为上下文(Context),只把事件转发给当前状态对象,
//     并提供 set_state() 让当前状态把自己换成另一个
//
// 互相依赖的「先有鸡还是先有蛋」用前向声明打破:
//   State 的方法要 MediaPlayer& 才能切换状态,而 MediaPlayer 又要持有 State。

class MediaPlayer; // 前向声明,让 State 能用 MediaPlayer&

// 状态的抽象接口:每个事件一个虚函数,外加一个 name() 方便打日志
struct State {
    virtual ~State() = default;
    virtual void play(MediaPlayer& ctx) = 0;
    virtual void pause(MediaPlayer& ctx) = 0;
    virtual void stop(MediaPlayer& ctx) = 0;
    virtual std::string name() const = 0; // 只是为了打日志
};

// 上下文:持有当前状态,把事件原样转发,并提供切换状态的入口
class MediaPlayer {
  public:
    explicit MediaPlayer(std::shared_ptr<State> s) : state_(std::move(s)) {}

    void set_state(std::shared_ptr<State> s) {
        std::cout << "[Context] " << state_->name() << " -> " << s->name() << "\n";
        state_ = std::move(s);
    }

    void play() { state_->play(*this); }
    void pause() { state_->pause(*this); }
    void stop() { state_->stop(*this); }

  private:
    std::shared_ptr<State> state_;
};

// 三个具体状态。需要切换状态的成员函数(像 StoppedState::play)只在此声明,
// 实现放到下面——否则 PlayingState 和 PausedState 互相构造会卡在
// 「还没定义完整」上,把实现拆到类外、放在三个类都声明完之后就能打破死结。

struct StoppedState : State {
    void play(MediaPlayer& ctx) override; // 切到 Playing,实现在下面
    void pause(MediaPlayer& /*ctx*/) override { std::cout << "[Stopped] pause() ignored\n"; }
    void stop(MediaPlayer& /*ctx*/) override { std::cout << "[Stopped] stop() already stopped\n"; }
    std::string name() const override { return "Stopped"; }
};

struct PlayingState : State {
    void play(MediaPlayer& /*ctx*/) override { std::cout << "[Playing] play() already playing\n"; }
    void pause(MediaPlayer& ctx) override; // 切到 Paused,实现在下面
    void stop(MediaPlayer& ctx) override;  // 切到 Stopped,实现在下面
    std::string name() const override { return "Playing"; }
};

struct PausedState : State {
    void play(MediaPlayer& ctx) override; // 切到 Playing,实现在下面
    void pause(MediaPlayer& /*ctx*/) override { std::cout << "[Paused] pause() already paused\n"; }
    void stop(MediaPlayer& ctx) override; // 切到 Stopped,实现在下面
    std::string name() const override { return "Paused"; }
};

// 三个具体状态此时都已是完整类型,make_shared<XxxState>() 写下去没有障碍。
inline void StoppedState::play(MediaPlayer& ctx) {
    std::cout << "[Stopped] start playing\n";
    ctx.set_state(std::make_shared<PlayingState>());
}

inline void PlayingState::pause(MediaPlayer& ctx) {
    std::cout << "[Playing] pausing\n";
    ctx.set_state(std::make_shared<PausedState>());
}

inline void PlayingState::stop(MediaPlayer& ctx) {
    std::cout << "[Playing] stopping\n";
    ctx.set_state(std::make_shared<StoppedState>());
}

inline void PausedState::play(MediaPlayer& ctx) {
    std::cout << "[Paused] resume\n";
    ctx.set_state(std::make_shared<PlayingState>());
}

inline void PausedState::stop(MediaPlayer& ctx) {
    std::cout << "[Paused] stop and back to initial\n";
    ctx.set_state(std::make_shared<StoppedState>());
}
