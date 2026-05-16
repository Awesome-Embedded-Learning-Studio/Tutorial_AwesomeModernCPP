#include <iostream>
#include <concepts>
#include <string>
#include <type_traits>

template<typename T>
concept HasMembers = std::is_class_v<T>;

template<typename T>
class SmartPtr {
    T* ptr_;
public:
    explicit SmartPtr(T* p = nullptr) : ptr_(p) {}
    ~SmartPtr() { delete ptr_; }
    SmartPtr(const SmartPtr&) = delete;
    SmartPtr& operator=(const SmartPtr&) = delete;
    T& operator*() const { return *ptr_; }
    T* operator->() const requires HasMembers<T> { return ptr_; }
};

int main() {
    SmartPtr<std::string> sp(new std::string("hello"));
    std::cout << *sp << std::endl;
    std::cout << sp->size() << std::endl;
    SmartPtr<int> ip(new int(42));
    std::cout << *ip << std::endl;
    return 0;
}
