// SequenceChecker:lazy 序列绑定 + debug 抓跨线程违规(release 下 0 字节 no-op)
// 来源:WeakPtr 前置知识(三):序列、SEQUENCE_CHECKER 与 DCHECK/CHECK (pre-03)
// 编译:g++ -std=c++17 -Wall -Wextra 14_sequence_checker.cpp -o 14_sequence_checker -pthread
//   debug:g++ -std=c++17 ... (默认,带 assert);release:g++ -std=c++17 -DNDEBUG ...

#include <cassert>
#include <iostream>
#include <thread>

namespace {

// 教学版:用 std::thread::id 模拟"序列"(Chromium 用 SequenceToken,更细)
// release(NDEBUG)下全 no-op,0 字节成员 —— 对应 SEQUENCE_CHECKER 三宏
#if defined(NDEBUG)
class SequenceChecker {
  public:
    void detach_from_sequence() noexcept {}
    bool called_on_valid_sequence() const noexcept { return true; }
};
#else
#    include <thread>
class SequenceChecker {
  public:
    void detach_from_sequence() noexcept { bound_ = std::thread::id{}; } // 构造时:未绑定
    bool called_on_valid_sequence() const noexcept {
        if (bound_ == std::thread::id{}) {
            bound_ = std::this_thread::get_id(); // lazy 绑定:首次触碰才定
            return true;
        }
        return bound_ == std::this_thread::get_id();
    }

  private:
    mutable std::thread::id bound_;
};
#endif

// 模拟 WeakPtr::Flag 的 lazy 绑定用法
class Flag {
  public:
    Flag() { seq_.detach_from_sequence(); } // 构造:未绑定
    void Invalidate() {
        assert(seq_.called_on_valid_sequence()); // 首次触碰 → 绑定到当前线程
        invalidated_ = true;
    }
    bool IsValid() const {
        assert(seq_.called_on_valid_sequence()); // 之后必须在同线程
        return !invalidated_;
    }

  private:
    SequenceChecker seq_;
    bool invalidated_ = false;
};

} // namespace

int main() {
    std::cout << "=== lazy 序列绑定(单线程,正常路径)==\n";
    Flag flag; // 构造在主线程,detach(未绑定)
    std::cout << "  主线程 IsValid=" << flag.IsValid() << "(首次触碰 → 绑定到主线程)\n";
    flag.Invalidate();
    std::cout << "  Invalidate 后 IsValid=" << flag.IsValid() << "\n";

    std::cout << "\n=== 跨线程违规(debug 下 assert 失败)==\n";
    std::cout << "  下面的注释代码演示违规:在另一线程调 IsValid 会触发 assert\n";
    // 取消下面注释在 debug 构建跑,会 abort:
    // std::thread t([&flag] { (void)flag.IsValid(); });   // 不在绑定线程 → assert 失败
    // t.join();

    std::cout << "\n=== release 构建(DNDEBUG)下零开销 ===\n";
    // 注意:本教学版用真实空类模拟 SequenceChecker,C++ 空类对象 sizeof = 1(最低 1 字节)。
    // 真实 Chromium 的 SEQUENCE_CHECKER(name) 宏在 release 下展开为 static_assert(true, ""),
    // 根本不生成成员 → 对宿主类 sizeof 贡献 0 字节(见文章 pre-03)。
    // 这里打印的是教学版空类的 sizeof,不是 Chromium 的真实行为。
    std::cout << "  教学版 sizeof(SequenceChecker) = " << sizeof(SequenceChecker)
              << "(空类,C++ 保证 ≥1 字节;debug 下为 thread::id 通常 8 字节)\n";
    std::cout << "  真实 Chromium:宏展开为 static_assert,0 字节成员 —— 比教学版更极致\n";
    return 0;
}
