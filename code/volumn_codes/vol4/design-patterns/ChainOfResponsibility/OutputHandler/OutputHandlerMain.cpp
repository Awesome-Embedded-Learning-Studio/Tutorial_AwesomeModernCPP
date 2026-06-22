#include "OutputHandler.h"

int main() {
    HandlerChain chain;
    chain.processMessageType({Message::DISK, "Hello, World"});
    chain.processMessageType({Message::CONSOLE, "Hello, World"});
    chain.processMessageType({Message::GUI_SCREEN, "Hello, World"});
}
