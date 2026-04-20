void func(int x) { }
void func(short x) { }

int main()
{
    func('A');  // 歧义？还是能编译？
    return 0;
}
