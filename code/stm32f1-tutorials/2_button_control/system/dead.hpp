#pragma once

namespace system::dead {
[[noreturn]] inline void halt(const char* raw_message [[maybe_unused]]) {
    while (1) {
    }
}
} // namespace system::dead
