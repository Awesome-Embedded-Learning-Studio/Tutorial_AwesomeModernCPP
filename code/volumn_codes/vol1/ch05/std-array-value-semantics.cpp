#include <array>
#include <iostream>

// 直接返回 std::array——C 数组做不到这一点
std::array<int, 5> make_array()
{
    std::array<int, 5> result = {1, 2, 3, 4, 5};
    return result;
}

// 按值传参——不会丢失大小信息
void print_array(std::array<int, 5> arr)
{
    for (int x : arr) {
        std::cout << x << " ";
    }
    std::cout << "\n函数内大小: " << arr.size() << "\n";
}

int main()
{
    auto arr1 = make_array();
    auto arr2 = arr1;  // 直接拷贝——C 数组做不到

    arr2[0] = 99;
    std::cout << "arr1[0] = " << arr1[0] << "\n";  // 1，不受 arr2 影响
    std::cout << "arr2[0] = " << arr2[0] << "\n";  // 99

    print_array(arr1);
    print_array(arr2);

    return 0;
}
