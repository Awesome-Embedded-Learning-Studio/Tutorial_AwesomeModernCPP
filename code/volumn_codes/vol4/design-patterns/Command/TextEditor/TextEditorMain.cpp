#include "TextEditor.h"

int main() {
    TextEditor editor;
    Invoker invoker;

    invoker.append_command(std::make_shared<AddCommand>("Hello, World"));
    invoker.append_command(std::make_shared<AddCommand>("Hello, World"));
    invoker.append_command(std::make_shared<EraseCommand>());
    invoker.append_command(std::make_shared<AddCommand>("Hello, World"));
    editor.process_invoker(&invoker);
    editor.dumpText();
}
