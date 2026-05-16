#include <iostream>

template<typename T>
T my_accumulate(T* begin, T* end, T init) {
    for (T* p = begin; p != end; ++p) init = init + *p;
    return init;
}

template int my_accumulate<int>(int*, int*, int);
template double my_accumulate<double>(double*, double*, double);

int main() {
    int arr1[] = {1, 2, 3, 4, 5};
    std::cout << my_accumulate(arr1, arr1 + 5, 0) << "\n";
    double arr2[] = {1.5, 2.5, 3.5};
    std::cout << my_accumulate(arr2, arr2 + 3, 0.0) << "\n";
}
