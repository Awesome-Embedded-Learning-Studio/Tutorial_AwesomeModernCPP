// Table-Driven Parser with Function Pointers
// Demonstrates ROM-friendly command parsing using function pointers

#include <iostream>
#include <cstring>
#include <cstdint>
#include <functional>

// Command handler type
using CommandHandler = void(*)(const char* args);

// Command structure
struct Command {
    const char* name;
    CommandHandler handler;
    const char* description;
};

// Handler implementations
void cmd_help(const char* args) {
    (void)args;  // Unused
    std::cout << "Available commands:" << std::endl;
    std::cout << "  help    - Show this help" << std::endl;
    std::cout << "  reset   - Reset system" << std::endl;
    std::cout << "  status  - Show system status" << std::endl;
    std::cout << "  config  - Configure [param] [value]" << std::endl;
    std::cout << "  ping    - Ping response" << std::endl;
}

void cmd_reset(const char* args) {
    (void)args;
    std::cout << "System resetting..." << std::endl;
}

void cmd_status(const char* args) {
    (void)args;
    std::cout << "System Status:" << std::endl;
    std::cout << "  Uptime: 12345 seconds" << std::endl;
    std::cout << "  Memory: 4096/8192 bytes free" << std::endl;
    std::cout << "  CPU: 15%" << std::endl;
}

void cmd_config(const char* args) {
    if (args && strlen(args) > 0) {
        std::cout << "Config: " << args << std::endl;
    } else {
        std::cout << "Usage: config <param> <value>" << std::endl;
    }
}

void cmd_ping(const char* args) {
    (void)args;
    std::cout << "pong" << std::endl;
}

// Command table (can be placed in ROM with appropriate attributes)
constexpr Command command_table[] = {
    {"help",   cmd_help,   "Show available commands"},
    {"reset",  cmd_reset,  "Reset the system"},
    {"status", cmd_status, "Show system status"},
    {"config", cmd_config, "Configure parameter"},
    {"ping",   cmd_ping,   "Ping-pong test"},
};

constexpr size_t num_commands = sizeof(command_table) / sizeof(command_table[0]);

// Command parser
class CommandParser {
public:
    void execute(const char* line) {
        if (!line || strlen(line) == 0) {
            return;
        }

        // Make a copy to modify
        char buffer[128];
        strncpy(buffer, line, sizeof(buffer) - 1);
        buffer[sizeof(buffer) - 1] = '\0';

        // Parse command and args
        char* cmd = buffer;
        char* args = nullptr;

        char* space = strchr(buffer, ' ');
        if (space) {
            *space = '\0';
            args = space + 1;
        }

        // Find and execute command
        for (size_t i = 0; i < num_commands; ++i) {
            if (strcmp(cmd, command_table[i].name) == 0) {
                std::cout << "> " << cmd;
                if (args) {
                    std::cout << " " << args;
                }
                std::cout << std::endl;
                command_table[i].handler(args);
                return;
            }
        }

        std::cout << "Unknown command: " << cmd << std::endl;
    }

    void list_commands() const {
        std::cout << "Available commands (" << num_commands << "):" << std::endl;
        for (size_t i = 0; i < num_commands; ++i) {
            std::cout << "  " << command_table[i].name
                      << " - " << command_table[i].description << std::endl;
        }
    }
};

// Interactive demo
void run_interactive() {
    std::cout << "=== Interactive Command Parser ===" << std::endl;
    std::cout << "Enter commands (empty line to exit):" << std::endl;

    CommandParser parser;
    parser.list_commands();

    char line[128];
    while (true) {
        std::cout << "\n> ";
        if (!std::cin.getline(line, sizeof(line))) {
            break;
        }

        if (strlen(line) == 0) {
            break;
        }

        parser.execute(line);
    }
}

// Batch demo
void run_batch() {
    std::cout << "=== Batch Command Parser ===" << std::endl;

    CommandParser parser;

    const char* commands[] = {
        "help",
        "status",
        "config baudrate 115200",
        "ping",
        "reset",
        "unknown_command",
    };

    for (const char* cmd : commands) {
        parser.execute(cmd);
        std::cout << std::endl;
    }
}

// Embedded-style buffer processing
void embedded_parser_demo() {
    std::cout << "=== Embedded Buffer Parser ===" << std::endl;

    // Simulate UART buffer
    const uint8_t uart_buffer[] = "status\r\n";
    size_t len = strlen(reinterpret_cast<const char*>(uart_buffer));

    // Find command (before \r\n)
    char cmd[16] = {0};
    for (size_t i = 0; i < len && i < sizeof(cmd) - 1; ++i) {
        if (uart_buffer[i] == '\r' || uart_buffer[i] == '\n') {
            break;
        }
        cmd[i] = uart_buffer[i];
    }

    std::cout << "Received: " << uart_buffer << std::endl;
    CommandParser parser;
    parser.execute(cmd);
}

// ROM placement discussion
void rom_discussion() {
    std::cout << "\n=== ROM Placement ===" << std::endl;
    std::cout << "With GCC/Clang, place command table in ROM using:" << std::endl;
    std::cout << "  __attribute__((section(\".rodata\")))" << std::endl;
    std::cout << "With ARMCC, use:" << std::endl;
    std::cout << "  __attribute__((section(\".rodata\")))" << std::endl;
    std::cout << "or const __attribute__((used))" << std::endl;
    std::cout << "\nBenefits:" << std::endl;
    std::cout << "- Zero RAM usage for command strings" << std::endl;
    std::cout << "- Fast access (flash is usually fast enough)" << std::endl;
    std::cout << "- Immutable (can't be corrupted)" << std::endl;
}

int main() {
    run_batch();
    std::cout << "\n---\n";
    embedded_parser_demo();
    rom_discussion();

    // Uncomment for interactive mode:
    // run_interactive();

    return 0;
}
