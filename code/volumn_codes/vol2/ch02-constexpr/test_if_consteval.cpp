// Test: Verify if consteval actually works as described
#include <cstdio>
#include <cstddef>

constexpr std::size_t compute_hash(const char* str, std::size_t len)
{
    if consteval {
        // Compile-time path
        std::size_t hash = 0xcbf29ce484222325ull;
        for (std::size_t i = 0; i < len; ++i) {
            hash ^= static_cast<std::size_t>(str[i]);
            hash *= 0x100000001b3ull;
        }
        return hash;
    } else {
        // Runtime path
        std::size_t hash = 0xcbf29ce484222325ull;
        for (std::size_t i = 0; i < len; ++i) {
            hash ^= static_cast<std::size_t>(str[i]);
            hash *= 0x100000001b3ull;
        }
        return hash;
    }
}

int main() {
    // Compile-time evaluation
    constexpr auto kCompileTimeHash = compute_hash("test", 4);

    // Runtime evaluation
    const char* runtime_str = "test";
    auto runtime_hash = compute_hash(runtime_str, 4);

    printf("Compile-time hash: %zu\n", kCompileTimeHash);
    printf("Runtime hash: %zu\n", runtime_hash);
    printf("Hashes match: %s\n", kCompileTimeHash == runtime_hash ? "yes" : "no");

    return 0;
}
