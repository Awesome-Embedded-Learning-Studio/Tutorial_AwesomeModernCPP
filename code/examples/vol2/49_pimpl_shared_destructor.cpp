#include <cstdio>
#include <memory>

// ---------------------------------------------------------------
// 同一行 "~Widget() = default" 写在类内（Impl 只有前向声明）：
//   unique_ptr<Impl> 版 → GCC 拒绝编译（delete 需要 Impl 完整）
//   shared_ptr<Impl> 版 → 编译、链接、运行全过
//
// unique_ptr 版长这样（想验证的话取消注释再编译）：
//
//   class UqWidget {
//   public:
//       UqWidget();
//       ~UqWidget() = default;   // error: invalid application of
//                                // 'sizeof' to incomplete type
//   private:
//       struct Impl;
//       std::unique_ptr<Impl> impl_;
//   };
// ---------------------------------------------------------------

class ShWidget {
  public:
    ShWidget();            // 构造不能留在类内：make_shared<Impl>()
                           // 需要 Impl 完整，只能声明后挪到"cpp 侧"
    ~ShWidget() = default; // 析构却可以留在类内：shared_ptr 的
                           // 删除动作在构造点已存进控制块
    int value() const;
    void bump();

  private:
    struct Impl; // 前向声明：到这里 Impl 不完整
    std::shared_ptr<Impl> impl_;
};

// ---- 以下等价于 widget.cpp：Impl 在这里才完整 ----
struct ShWidget::Impl {
    int count = 0;
};

ShWidget::ShWidget() : impl_(std::make_shared<Impl>()) {}
int ShWidget::value() const {
    return impl_->count;
}
void ShWidget::bump() {
    ++impl_->count;
}

int main() {
    ShWidget w;
    w.bump();
    w.bump();
    std::printf("count = %d\n", w.value());
    return 0;
}
