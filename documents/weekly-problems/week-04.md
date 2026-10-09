### 第1题-编程题：实现一个gcd函数

gcd函数在很多地方都会用到，所以，让我们来实现一个自己的gcd函数吧~UwU

#### 参考答案：

```c
int gcd(int n, int m)
{
    if (n <= 0 || m <= 0)
        return -1;
    if (n > m)
    {
        if (n % m == 0)
            return m;
        return gcd(m, n % m);
    }
    if (n < m)
    {
        if (m % n == 0)
            return n;
        return gcd(n, m % n);
    }
    return n;
}

```

**题目来源-经典题目**

---

### 第2题-编程题：max_list

编写一个名叫max_list的函数，它用于检查任意数目的整型参数并返回它们中的最大值。参数列表必须以一个负值结尾，提示列表的结束。

#### 参考答案：

```c
int max_list(int first, ...)
{
	if(first < 0)
		return -1;
	va_list args;
	int max = first;
	va_start(args, first);
	int tmp;
	while ((tmp = va_arg(args, int)) >= 0)
	{
		if (tmp > max)
			max = tmp;
	}
	va_end(args);
	return max;
}
```

**题目来源-经典题目**

---

### 第3题-编程题：getchar/putchar的初等应用

用 getchar/putchar 写程序：把输入中所有小写字母转成大写，其余原样，正确处理 EOF。
例如：
输入：abcd
输出：ABCD

#### 参考答案

```c
void words_upper()
{
    int ch = 0;
    int dif = 'A' - 'a';
    while((ch = getchar()) != EOF)
        {
            if(ch >= 'a' && ch <= 'z' )
                putchar((ch + dif));
            else
                putchar(ch);
        }
    return ;
}
```

**题目来源-《C和指针》**

---

### 第4题-编程题：checksum函数

checksum 程序——逐字符复制输入到输出，signed char 检验和从 -1 起累加、溢出忽略，最后以十进制打印并换行。

例如：
输入："Hello world!\n"
输出： 102

#### 参考答案

```c
void checksum()
{
    int ch = 0;
    signed char sum = -1;
    while((ch = getchar()) != EOF)
    {
        sum += ch;
    }
    printf("\n%d\n",sum);
}
```

**题目来源-《C和指针》**
