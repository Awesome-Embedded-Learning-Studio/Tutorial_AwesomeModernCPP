#pragma once
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

// 备忘录模式(Memento):把「状态」封进谁也读不懂的黑盒。
//
// 发起者(Originator)是 TextEditor,备忘录是它的嵌套类 Memento。
// Memento 的构造函数和字段全部 private,唯一的 friend 是 TextEditor 自己,
// 于是整个程序里只有 TextEditor 能构造/读写 Memento;撤销栈(History)拿到
// 的只是一个连 content 都看不见的不透明句柄——这就是 GoF 说的「窄接口」。
class TextEditor {
  public:
    // 备忘录是 Originator 的嵌套类型,对外是个黑盒。
    class Memento {
        friend class TextEditor; // 只有 TextEditor 能访问下面这些
        std::string content;
        std::size_t cursor_pos = 0;

        Memento(std::string c, std::size_t p) : content(std::move(c)), cursor_pos(p) {}

      public:
        // 外部(含 Caretaker)只能拷贝/移动这个不透明句柄,读不到内容。
        Memento() = default;
        Memento(const Memento&) = default;
        Memento(Memento&&) = default;
        Memento& operator=(const Memento&) = default;
        Memento& operator=(Memento&&) = default;
    };

    std::shared_ptr<Memento> create_memento() const {
        // TextEditor 是 Memento 的 friend,这里直接调私有构造,合法。
        // 不走 make_shared,否则 allocator_traits::construct 会撞访问控制。
        return std::shared_ptr<Memento>(new Memento(content_, cursor_pos_));
    }

    void restore(const std::shared_ptr<Memento>& m) {
        if (!m)
            return;
        content_ = m->content; // friend 授权:这里能访问私有字段
        cursor_pos_ = m->cursor_pos;
    }

    void insert(const std::string& s) {
        content_.insert(cursor_pos_, s);
        cursor_pos_ += s.size();
    }

    std::string get_content() const { return content_; }
    std::size_t get_cursor_pos() const { return cursor_pos_; }

  private:
    std::string content_;
    std::size_t cursor_pos_ = 0;
};

// 管理者(Caretaker):一条带指针的线性历史栈。
// undo 是指针后退,redo 是指针前进;在非末尾处插入新快照时丢弃之后的 redo 分支,
// 这正是所有编辑器「撤销两步、再敲个新字符,原来的重做链路就没了」的行为模型。
class History {
  public:
    void push(std::shared_ptr<TextEditor::Memento> m) {
        // 在非末尾处插入新快照时,丢弃之后的 redo 分支。
        if (cursor_ + 1 < static_cast<int>(stack_.size())) {
            stack_.erase(stack_.begin() + cursor_ + 1, stack_.end());
        }
        stack_.push_back(std::move(m));
        cursor_ = static_cast<int>(stack_.size()) - 1;
    }

    bool can_undo() const { return cursor_ > 0; }
    bool can_redo() const { return cursor_ + 1 < static_cast<int>(stack_.size()); }

    std::shared_ptr<TextEditor::Memento> undo() {
        if (!can_undo())
            return nullptr;
        --cursor_;
        return stack_[static_cast<std::size_t>(cursor_)];
    }

    std::shared_ptr<TextEditor::Memento> redo() {
        if (!can_redo())
            return nullptr;
        ++cursor_;
        return stack_[static_cast<std::size_t>(cursor_)];
    }

  private:
    std::vector<std::shared_ptr<TextEditor::Memento>> stack_;
    int cursor_ = -1; // -1 表示空历史
};
