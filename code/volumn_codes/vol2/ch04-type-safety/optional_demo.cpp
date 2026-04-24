#include <optional>
#include <string>
#include <vector>
#include <iostream>
#include <cstdint>

// === 传统方案对比 ===

int find_index_old(const std::vector<int>& v, int target)
{
    for (int i = 0; i < static_cast<int>(v.size()); ++i) {
        if (v[i] == target) return i;
    }
    return -1;
}

// === optional 核心语义 ===

std::optional<std::size_t> find_index(
    const std::vector<int>& v, int target)
{
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (v[i] == target) return i;
    }
    return std::nullopt;
}

// === optional 作为参数 ===

void print_greeting(const std::string& name,
                    std::optional<std::string> title = std::nullopt)
{
    if (title) {
        std::cout << "Hello, " << *title << " " << name << "!\n";
    } else {
        std::cout << "Hello, " << name << "!\n";
    }
}

// === 工厂函数 ===

class Connection {
public:
    static std::optional<Connection> create(const std::string& addr)
    {
        if (addr.empty()) return std::nullopt;
        return Connection(addr);
    }

    std::string address() const { return addr_; }

private:
    explicit Connection(std::string addr) : addr_(std::move(addr)) {}
    std::string addr_;
};

// === 延迟初始化 ===

class ExpensiveResource {
public:
    ExpensiveResource() { std::cout << "  Resource initialized\n"; }
    void do_work() { std::cout << "  Resource working\n"; }
};

class Service {
public:
    void process()
    {
        if (!resource_) {
            std::cout << "Lazy init:\n";
            resource_.emplace();
        }
        resource_->do_work();
    }

private:
    std::optional<ExpensiveResource> resource_;
};

// === C++23 monadic 操作 ===

std::optional<int> parse_int(const std::string& s)
{
    try {
        return std::stoi(s);
    } catch (...) {
        return std::nullopt;
    }
}

int main()
{
    // 查找操作
    std::vector<int> data = {10, 20, 30, 40, 50};

    std::cout << "--- find_index (optional) ---\n";
    auto idx = find_index(data, 30);
    if (idx) {
        std::cout << "found at index " << *idx << "\n";
    }

    auto not_found = find_index(data, 99);
    if (!not_found) {
        std::cout << "99 not found\n";
    }

    // sizeof 对比
    std::cout << "\n--- sizeof ---\n";
    std::cout << "sizeof(int):              " << sizeof(int) << "\n";
    std::cout << "sizeof(optional<int>):    " << sizeof(std::optional<int>) << "\n";
    std::cout << "sizeof(double):           " << sizeof(double) << "\n";
    std::cout << "sizeof(optional<double>): " << sizeof(std::optional<double>) << "\n";
    std::cout << "sizeof(string):           " << sizeof(std::string) << "\n";
    std::cout << "sizeof(optional<string>): " << sizeof(std::optional<std::string>) << "\n";

    // value_or
    std::cout << "\n--- value_or ---\n";
    std::optional<int> opt_val = 42;
    std::cout << "has value: " << opt_val.value_or(0) << "\n";
    std::optional<int> opt_empty;
    std::cout << "no value:  " << opt_empty.value_or(0) << "\n";

    // optional 参数
    std::cout << "\n--- optional parameter ---\n";
    print_greeting("Alice");
    print_greeting("Bob", std::string("Dr."));

    // 工厂函数
    std::cout << "\n--- factory ---\n";
    auto conn = Connection::create("192.168.1.1");
    if (conn) {
        std::cout << "Connected to " << conn->address() << "\n";
    }

    auto failed = Connection::create("");
    if (!failed) {
        std::cout << "Connection failed (empty addr)\n";
    }

    // 延迟初始化
    std::cout << "\n--- lazy init ---\n";
    Service svc;
    std::cout << "First call:\n";
    svc.process();
    std::cout << "Second call:\n";
    svc.process();

    // C++23 monadic
#if __cpp_lib_optional >= 202110L
    std::cout << "\n--- C++23 monadic ---\n";
    auto result = parse_int("42")
        .transform([](int v) { return v * 2; })
        .transform([](int v) { return std::to_string(v); });
    if (result) {
        std::cout << "Result: " << *result << "\n";
    }
#else
    std::cout << "\n--- C++20 style fallback ---\n";
    auto parsed = parse_int("42");
    if (parsed) {
        auto result = std::to_string(*parsed * 2);
        std::cout << "Result: " << result << "\n";
    }
#endif

    return 0;
}
