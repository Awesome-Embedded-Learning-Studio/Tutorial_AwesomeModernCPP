#pragma once

#include <cstdint>
#include <variant>

namespace device {

struct Pressed {};
struct Released {};

using ButtonEvent = std::variant<Pressed, Released>;

} // namespace device
