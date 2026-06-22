#pragma once
#include <algorithm>
#include <memory>
#include <print>
#include <string>
#include <vector>
struct TextEditorCommand {
    enum class Type { APPEND, REMOVE };
    TextEditorCommand(Type t) : type(t) {}
    Type get_type() const { return type; }
    virtual ~TextEditorCommand() = default;

  private:
    const Type type;
};

struct AddCommand : public TextEditorCommand {
    std::string append;
    AddCommand(std::string buffer)
        : append(std::move(buffer)), TextEditorCommand(TextEditorCommand::Type::APPEND) {

          };
};

struct EraseCommand : public TextEditorCommand {
    EraseCommand() : TextEditorCommand(TextEditorCommand::Type::REMOVE) {}
};

struct Invoker {
    friend struct TextEditor;
    void append_command(std::shared_ptr<TextEditorCommand> command) {
        commands.push_back(std::move(command));
    }

    void remove_command(std::shared_ptr<TextEditorCommand> command) {
        commands.erase(std::remove(commands.begin(), commands.end(), command), commands.end());
    }

    void simplified() {
        std::vector<std::shared_ptr<TextEditorCommand>> result;
        for (const auto& cmd : commands) {
            if (!result.empty() && result.back()->get_type() == TextEditorCommand::Type::APPEND &&
                cmd->get_type() == TextEditorCommand::Type::REMOVE) {
                result.pop_back();
            } else {
                result.push_back(cmd);
            }
        }
        commands = std::move(result);
    }

  private:
    std::vector<std::shared_ptr<TextEditorCommand>> commands;
};

struct TextEditor {

    void process_invoker(Invoker* Invoker) {
        Invoker->simplified();
        for (auto& command : Invoker->commands) {
            if (command->get_type() == TextEditorCommand::Type::REMOVE) {
                pop_text_once();
            } else {
                AddCommand* adder = dynamic_cast<AddCommand*>(command.get());
                if (adder)
                    append_text(adder->append);
            }
        }
    }

    void dumpText() {
        for (const auto& each : current_src) {
            std::println("{}", each);
        }
    }

  protected:
    void append_text(const std::string& buffer) { current_src.emplace_back(buffer); }

    void pop_text_once() {
        if (!current_src.empty()) {
            current_src.pop_back();
        }
    }

  private:
    std::vector<std::string> current_src;
};
