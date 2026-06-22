#include "logger.h"
#include <print>

SimpleLogger::SimpleLogger() {
    std::println("[logger: ] Logger init invoke once");
}

SimpleLogger& SimpleLogger::instance() {
    static SimpleLogger simpleLogger;
    return simpleLogger;
}

void SimpleLogger::log_messages(const std::string& message) {
    std::println("[logger: ] {}", message);
}
