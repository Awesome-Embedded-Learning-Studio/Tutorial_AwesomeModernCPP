void add_ten(int& x)
{
    x += 10;
}

int main()
{
    int value = 5;
    add_ten(value);
    // value 现在是 15
    return 0;
}
