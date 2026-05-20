// std::atomic operations: load/store, fetch_add, CAS, atomic_flag
#include <atomic>
#include <cstdint>
#include <iostream>

int main() {
    // load / store
    std::cout << "=== load / store ===\n";
    std::atomic<int> value{0};
    value.store(42);
    std::cout << "value = " << value.load() << "\n";

    // fetch_add / fetch_sub
    std::cout << "\n=== fetch_add / fetch_sub ===\n";
    std::atomic<int> counter{0};
    int old1 = counter.fetch_add(5); // counter=5,  old1=0
    int old2 = counter.fetch_add(3); // counter=8,  old2=5
    int old3 = counter.fetch_sub(2); // counter=6,  old3=8
    std::cout << "counter = " << counter.load() << " (old1=" << old1 << " old2=" << old2
              << " old3=" << old3 << ")\n";

    // compare_exchange (CAS)
    std::cout << "\n=== compare_exchange_strong ===\n";
    std::atomic<int> cas_val{10};

    int expected = 10;
    bool ok = cas_val.compare_exchange_strong(expected, 20);
    std::cout << "CAS(10->20): ok=" << ok << " value=" << cas_val.load() << "\n";

    expected = 10; // try again with stale expected
    ok = cas_val.compare_exchange_strong(expected, 30);
    std::cout << "CAS(10->30): ok=" << ok << " value=" << cas_val.load() << " expected=" << expected
              << "\n";

    // exchange
    std::cout << "\n=== exchange ===\n";
    std::atomic<int> flag{0};
    int prev = flag.exchange(1);
    std::cout << "exchange(0->1): prev=" << prev << " flag=" << flag.load() << "\n";

    // is_lock_free
    std::cout << "\n=== is_lock_free ===\n";
    std::atomic<int> ai;
    std::atomic<int64_t> all;
    std::cout << "atomic<int>:     " << (ai.is_lock_free() ? "lock-free" : "uses lock") << "\n";
    std::cout << "atomic<int64_t>: " << (all.is_lock_free() ? "lock-free" : "uses lock") << "\n";

    static_assert(std::atomic<int>::is_always_lock_free, "int must be lock-free on this platform!");

    // atomic_flag: guaranteed lock-free spinlock primitive
    std::cout << "\n=== atomic_flag ===\n";
    std::atomic_flag af{};
    bool was_set = af.test_and_set();
    std::cout << "test_and_set: was_set=" << was_set << " now=" << af.test() << "\n";
    af.clear();
    std::cout << "after clear: " << af.test() << "\n";

    return 0;
}
