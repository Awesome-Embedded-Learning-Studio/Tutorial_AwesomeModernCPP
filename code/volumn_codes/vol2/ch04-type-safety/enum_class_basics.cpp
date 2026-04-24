#include <cstdio>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

// === 位运算与 enum class ===

enum class Permission : uint32_t {
    kNone    = 0,
    kRead    = 1 << 0,
    kWrite   = 1 << 1,
    kExecute = 1 << 2
};

template <typename E>
constexpr auto to_underlying(E e) noexcept
{
    return static_cast<std::underlying_type_t<E>>(e);
}

constexpr Permission operator|(Permission a, Permission b) noexcept
{
    return static_cast<Permission>(to_underlying(a) | to_underlying(b));
}

constexpr Permission operator&(Permission a, Permission b) noexcept
{
    return static_cast<Permission>(to_underlying(a) & to_underlying(b));
}

constexpr Permission operator~(Permission a) noexcept
{
    return static_cast<Permission>(~to_underlying(a));
}

constexpr Permission& operator|=(Permission& a, Permission b) noexcept
{
    a = a | b;
    return a;
}

constexpr Permission& operator&=(Permission& a, Permission b) noexcept
{
    a = a & b;
    return a;
}

constexpr bool has_flag(Permission flags, Permission flag) noexcept
{
    return to_underlying(flags & flag) != 0;
}

// === switch 匹配 ===

enum class NetworkState : uint8_t {
    kDisconnected,
    kConnecting,
    kConnected,
    kError
};

std::string_view to_string(NetworkState state)
{
    switch (state) {
    case NetworkState::kDisconnected: return "disconnected";
    case NetworkState::kConnecting:   return "connecting";
    case NetworkState::kConnected:    return "connected";
    case NetworkState::kError:        return "error";
    }
    return "unknown";
}

// === C++20 using enum ===

enum class TokenType {
    kNumber, kString, kIdentifier,
    kPlus, kMinus, kStar, kSlash,
    kLeftParen, kRightParen, kEof
};

std::string_view token_to_string(TokenType type)
{
    using enum TokenType;

    switch (type) {
    case kNumber:     return "number";
    case kString:     return "string";
    case kIdentifier: return "identifier";
    case kPlus:       return "+";
    case kMinus:      return "-";
    case kStar:       return "*";
    case kSlash:      return "/";
    case kLeftParen:  return "(";
    case kRightParen: return ")";
    case kEof:        return "eof";
    }
    return "unknown";
}

// === 状态机 ===

enum class DeviceState : uint8_t {
    kIdle,
    kInitializing,
    kRunning,
    kSuspending,
    kError
};

class DeviceController {
public:
    void on_event(const char* event)
    {
        switch (state_) {
        case DeviceState::kIdle:
            if (is_start(event)) {
                state_ = DeviceState::kInitializing;
                std::printf("State: Idle -> Initializing\n");
            }
            break;
        case DeviceState::kInitializing:
            if (is_init_done(event)) {
                state_ = DeviceState::kRunning;
                std::printf("State: Initializing -> Running\n");
            } else if (is_error(event)) {
                state_ = DeviceState::kError;
                std::printf("State: Initializing -> Error\n");
            }
            break;
        case DeviceState::kRunning:
            if (is_stop(event)) {
                state_ = DeviceState::kSuspending;
                std::printf("State: Running -> Suspending\n");
            } else if (is_error(event)) {
                state_ = DeviceState::kError;
                std::printf("State: Running -> Error\n");
            }
            break;
        case DeviceState::kSuspending:
            if (is_suspend_done(event)) {
                state_ = DeviceState::kIdle;
                std::printf("State: Suspending -> Idle\n");
            }
            break;
        case DeviceState::kError:
            if (is_reset(event)) {
                state_ = DeviceState::kIdle;
                std::printf("State: Error -> Idle\n");
            }
            break;
        }
    }

    DeviceState get_state() const noexcept { return state_; }

private:
    DeviceState state_ = DeviceState::kIdle;

    static bool is_start(const char* e)          { return e[0] == 'S'; }
    static bool is_init_done(const char* e)      { return e[0] == 'D'; }
    static bool is_stop(const char* e)           { return e[0] == 'T'; }
    static bool is_suspend_done(const char* e)   { return e[0] == 's'; }
    static bool is_error(const char* e)          { return e[0] == 'E'; }
    static bool is_reset(const char* e)          { return e[0] == 'R'; }
};

// === 错误码 ===

enum class ErrorCode : int {
    kOk = 0,
    kInvalidArgument = 1,
    kNotFound = 2,
    kPermissionDenied = 3,
    kTimeout = 4,
    kInternalError = 5
};

struct Result {
    ErrorCode code;
    std::string_view message;

    bool is_ok() const noexcept { return code == ErrorCode::kOk; }
};

Result open_file(const char* path)
{
    if (!path || path[0] == '\0') {
        return {ErrorCode::kInvalidArgument, "path is empty"};
    }
    return {ErrorCode::kOk, "success"};
}

int main()
{
    // 位运算演示
    Permission user_perms = Permission::kRead | Permission::kWrite;
    std::printf("has Read:    %d\n", has_flag(user_perms, Permission::kRead));
    std::printf("has Execute: %d\n", has_flag(user_perms, Permission::kExecute));

    user_perms |= Permission::kExecute;
    std::printf("after add Execute: %d\n", has_flag(user_perms, Permission::kExecute));

    user_perms &= ~Permission::kWrite;
    std::printf("after remove Write: %d\n", has_flag(user_perms, Permission::kWrite));

    // switch 匹配
    std::printf("\nNetworkState: %s\n", to_string(NetworkState::kConnected).data());

    // using enum
    std::printf("TokenType: %s\n", token_to_string(TokenType::kPlus).data());

    // 状态机
    std::printf("\n--- State Machine ---\n");
    DeviceController ctrl;
    ctrl.on_event("Start");    // Idle -> Initializing
    ctrl.on_event("Done");     // Initializing -> Running
    ctrl.on_event("Stop");     // Running -> Suspending
    ctrl.on_event("suspend");  // Suspending -> Idle

    // 错误码
    std::printf("\n--- Error Code ---\n");
    auto r1 = open_file("/tmp/test.txt");
    std::printf("open valid:   ok=%d, msg=%s\n", r1.is_ok(), r1.message.data());

    auto r2 = open_file("");
    std::printf("open empty:   ok=%d, msg=%s\n", r2.is_ok(), r2.message.data());

    // 前向声明验证
    static_assert(sizeof(Permission) == 4, "Permission should be 4 bytes");
    static_assert(sizeof(NetworkState) == 1, "NetworkState should be 1 byte");

    return 0;
}
