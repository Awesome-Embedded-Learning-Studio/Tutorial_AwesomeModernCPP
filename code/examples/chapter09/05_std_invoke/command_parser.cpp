// Command Parser using std::invoke
// Demonstrates a CLI system that handles different callable types uniformly

#include <iostream>
#include <unordered_map>
#include <sstream>
#include <string>
#include <functional>

class CommandParser {
public:
    // Register any callable as a command handler
    template<typename Func>
    void register_command(const std::string& name, Func&& handler) {
        commands_[name] = CommandWrapper{std::forward<Func>(handler)};
    }

    // Execute command
    bool execute(const std::string& line) {
        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;

        auto it = commands_.find(cmd);
        if (it == commands_.end()) {
            std::cout << "Unknown command: " << cmd << std::endl;
            return false;
        }

        return it->second.invoke(iss);
    }

private:
    struct CommandWrapper {
        std::function<bool(std::istringstream&)> invoke;
    };

    std::unordered_map<std::string, CommandWrapper> commands_;
};

// Global function handler
bool handle_set(std::istringstream& iss) {
    std::string key, value;
    iss >> key >> value;
    std::cout << "Set " << key << " = " << value << std::endl;
    return true;
}

bool handle_list(std::istringstream&) {
    std::cout << "Listing all entries..." << std::endl;
    return true;
}

// Class with member function handlers
class ConfigManager {
    std::unordered_map<std::string, int> config_;

public:
    bool handle_get(std::istringstream& iss) {
        std::string key;
        iss >> key;
        auto it = config_.find(key);
        if (it != config_.end()) {
            std::cout << key << " = " << it->second << std::endl;
        } else {
            std::cout << key << " not found" << std::endl;
        }
        return true;
    }

    bool handle_delete(std::istringstream& iss) {
        std::string key;
        iss >> key;
        if (config_.erase(key)) {
            std::cout << "Deleted " << key << std::endl;
        } else {
            std::cout << key << " not found" << std::endl;
        }
        return true;
    }

    void add_config(const std::string& key, int value) {
        config_[key] = value;
    }
};

// Helper to create command wrapper from member function
template<typename T>
auto make_command(T* obj, bool (T::*method)(std::istringstream&)) {
    return [obj, method](std::istringstream& iss) {
        return std::invoke(method, obj, iss);
    };
}

int main() {
    std::cout << "=== Command Parser Demo ===" << std::endl;

    CommandParser parser;
    ConfigManager config;

    // Add some initial config
    config.add_config("baudrate", 115200);
    config.add_config("timeout", 1000);

    // Register global functions
    parser.register_command("set", handle_set);
    parser.register_command("list", handle_list);

    // Register member functions using std::invoke
    parser.register_command("get", [&config](std::istringstream& iss) {
        return std::invoke(&ConfigManager::handle_get, config, iss);
    });

    parser.register_command("delete", [&config](std::istringstream& iss) {
        return std::invoke(&ConfigManager::handle_delete, config, iss);
    });

    // Register lambda
    parser.register_command("help", [](std::istringstream&) {
        std::cout << "Available commands: set, get, delete, list, help" << std::endl;
        return true;
    });

    // Execute commands
    std::cout << "\n--- Executing Commands ---" << std::endl;

    parser.execute("help");
    parser.execute("get baudrate");
    parser.execute("set address 0x1000");
    parser.execute("get timeout");
    parser.execute("list");
    parser.execute("delete timeout");
    parser.execute("unknown");

    std::cout << "\n=== Key Benefits ===" << std::endl;
    std::cout << "- Uniform interface for all callable types" << std::endl;
    std::cout << "- Easy to add global functions, member functions, or lambdas" << std::endl;
    std::cout << "- std::invoke handles member function syntax automatically" << std::endl;
    std::cout << "- Type-safe at compile time" << std::endl;

    return 0;
}
