#include <iostream>

void print_array(int arr[])
{
    std::cout << "sizeof(arr) = " << sizeof(arr) << std::endl;
}

int main()
{
    int data[5] = {1, 2, 3, 4, 5};
    std::cout << "sizeof(data) = " << sizeof(data) << std::endl;
    print_array(data);
    return 0;
}
