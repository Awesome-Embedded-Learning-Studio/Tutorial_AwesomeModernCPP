int compute(int x)
{
    int result = x * 2;  // result 是局部变量
    return result;
}   // result 在这里被销毁

int main()
{
    int r = compute(5);
    // std::cout << result;  // 编译错误！result 不在作用域内
    return 0;
}
