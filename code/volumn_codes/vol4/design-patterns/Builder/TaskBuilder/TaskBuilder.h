#pragma once
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

// 业务对象 Task:只管自己的业务语义,构造细节全交给 TaskBuilder。
// 与文章第 02 步的 Task 完全一致:必填项进构造函数并校验,
// 选填项用 setter 后配,std::optional 表达「可能没填」。
class Task {
  public:
    enum class Priority { Immediate, High, Medium, Low };
    struct CTime {
        int year, month, day, hour, minute, second;
    };

    // 必填:优先级、截止时间、描述
    Task(Priority p, CTime ddl, const std::string& desc)
        : priority_(p), ddl_(ddl), description_(desc) {
        if (desc.empty()) {
            throw std::invalid_argument("Invalid Task Description");
        }
    }

    void set_title(std::string t) { title_ = std::move(t); }
    void set_details(std::string d) { details_ = std::move(d); }

    Priority priority() const { return priority_; }
    const CTime& ddl() const { return ddl_; }
    const std::string& description() const { return description_; }
    const std::optional<std::string>& title() const { return title_; }
    const std::optional<std::string>& details() const { return details_; }

    void do_work() const {
        // 业务方法:打印一行,便于在 main 里观察对象字段是否齐全
    }

  private:
    Priority priority_;
    CTime ddl_;
    std::string description_;
    std::optional<std::string> title_;
    std::optional<std::string> details_;
};

// 流式构建器:每个 with_* 设完字段后返回 *this,调用链流动起来。
// 字段「填没填」用 std::optional 内化进类型,不再背 is_xxx_set 标志位。
// build() 在必填项没填齐时抛异常(文章把失败策略从 std::optional 换成抛异常,
// 以便把「漏填」当作程序员犯糊涂的逻辑错误冒泡到上层)。
class TaskBuilder {
  public:
    TaskBuilder& with_priority(Task::Priority p) {
        priority_ = p;
        return *this;
    }
    TaskBuilder& with_ddl(Task::CTime d) {
        ddl_ = d;
        return *this;
    }
    TaskBuilder& with_description(std::string s) {
        description_ = std::move(s);
        return *this;
    }
    TaskBuilder& with_title(std::string t) {
        title_ = std::move(t);
        return *this;
    }
    TaskBuilder& with_details(std::string d) {
        details_ = std::move(d);
        return *this;
    }

    Task build() const {
        if (!priority_ || !ddl_ || !description_) {
            throw std::runtime_error("Cannot build Task: missing required field");
        }
        Task t(*priority_, *ddl_, *description_);
        if (title_)
            t.set_title(*title_);
        if (details_)
            t.set_details(*details_);
        return t; // RVO:C++17 mandatory copy elision,零拷贝
    }

  private:
    std::optional<Task::Priority> priority_;
    std::optional<Task::CTime> ddl_;
    std::optional<std::string> description_;
    std::optional<std::string> title_;
    std::optional<std::string> details_;
};

// 文章里的 RVO 验证工具类:挂 move/copy 计数器,
// 用来实测 build() 里 return t; 真的被强制省略。
class Tracked {
  public:
    int v;
    static inline int kMoveCount = 0;
    static inline int kCopyCount = 0;

    Tracked() : v(0) {}
    explicit Tracked(int x) : v(x) {}
    Tracked(Tracked&& o) noexcept : v(o.v) { ++kMoveCount; }
    Tracked(const Tracked& o) : v(o.v) { ++kCopyCount; }
};

class TrackedBuilder {
  public:
    TrackedBuilder& with_value(int x) {
        value_ = x;
        return *this;
    }
    Tracked build() const {
        Tracked t(*value_); // 局部对象
        return t;           // 预期被 NRVO / RVO 消除
    }

  private:
    std::optional<int> value_;
};
