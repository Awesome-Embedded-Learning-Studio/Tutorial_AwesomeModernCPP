#include "TextEditor.h"
#include <iostream>
#include <print>

namespace {
void dump_editor(const TextEditor& editor) {
    std::println("Content: \"{}\" | Cursor@{}", editor.get_content(), editor.get_cursor_pos());
}
} // namespace

int main() {
    TextEditor editor;
    History history;

    // 起点快照
    history.push(editor.create_memento());

    // 插入两段文本:Hello, 然后 ", world"
    editor.insert("Hello");
    history.push(editor.create_memento());
    editor.insert(", world");
    history.push(editor.create_memento());
    dump_editor(editor);

    // 连续撤销两次:Hello, world -> Hello -> ""
    if (auto snap = history.undo()) {
        editor.restore(snap);
        std::print("[undo] ");
        dump_editor(editor);
    }
    if (auto snap = history.undo()) {
        editor.restore(snap);
        std::print("[undo] ");
        dump_editor(editor);
    }

    // 重做一次:"" -> Hello
    if (auto snap = history.redo()) {
        editor.restore(snap);
        std::print("[redo] ");
        dump_editor(editor);
    }

    // 在 redo 中途插入新编辑,原 redo 未来应被丢弃
    editor.insert("!!!");
    history.push(editor.create_memento());
    std::print("[edit] ");
    dump_editor(editor);

    // 验证 redo 分支已被丢弃、当前状态仍可撤销
    std::println("can_redo = {} (expect 0)", history.can_redo() ? 1 : 0);
    std::println("can_undo = {} (expect 1)", history.can_undo() ? 1 : 0);

    return 0;
}
