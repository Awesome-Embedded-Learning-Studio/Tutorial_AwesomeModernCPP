/// @file rvo_asm_verify.cpp
/// @brief Heavy struct for verifying assembly output of RVO vs non-RVO
/// @note GCC 15, -std=c++17 -O2
///        sizeof(Heavy) = sizeof(int[256]) = 1024 bytes
///        without_rvo uses rep movsq with ecx=128 (128 qwords = 1024 bytes)

struct Heavy {
    int data[256];  // 256 * 4 = 1024 bytes
    Heavy(int v) { for (auto& d : data) d = v; }
    Heavy(const Heavy& o) { for (int i = 0; i < 256; ++i) data[i] = o.data[i]; }
    Heavy(Heavy&& o) noexcept { for (int i = 0; i < 256; ++i) data[i] = o.data[i]; }
};

/// C++17 guaranteed elision: prvalue return
Heavy with_rvo(int v)
{
    return Heavy(v);
}

/// No elision: returning parameter
Heavy without_rvo(Heavy h)
{
    return h;
}

// Prevent full optimization away
int main()
{
    volatile int x = 42;
    auto a = with_rvo(x);
    volatile int sink = a.data[0];
    (void)sink;

    Heavy h(x);
    auto b = without_rvo(static_cast<Heavy&&>(h));
    sink = b.data[0];
    (void)sink;

    return 0;
}
