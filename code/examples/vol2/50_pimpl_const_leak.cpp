#include <cstdio>
#include <memory>

class Counter {
  public:
    Counter() : impl_(std::make_unique<Impl>()) {}

    // inc 声明成 const 成员函数，内部却在改状态——能编译能运行。
    // 原因：impl_ 是 const 的 unique_ptr，但 operator-> 返回的是
    // 非 const 的 Impl*；指针本身 const 不等于指向物 const。
    void inc() const { ++impl_->count; }

    int get() const { return impl_->count; }

  private:
    struct Impl {
        int count = 0;
    };
    std::unique_ptr<Impl> impl_;
};

// 对照组：成员直写在类里时，const 成员函数改成员直接被编译器拦下：
//
//   class NaiveCounter {
//   public:
//       void inc() const { ++count_; }   // error: increment of member
//   private:                             // 'count_' in read-only object
//       int count_ = 0;
//   };

int main() {
    const Counter c; // const 对象
    c.inc();         // 居然真的改得动
    c.inc();
    std::printf("const 对象 get() = %d\n", c.get());
    return 0;
}
