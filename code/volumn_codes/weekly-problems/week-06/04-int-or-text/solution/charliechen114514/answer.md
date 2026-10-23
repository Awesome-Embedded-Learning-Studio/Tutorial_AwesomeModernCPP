**思路**:

骨架是「读一行、剥换行、判字符、分型输出」。咱们先把判定的规矩立住:整行非空;第一个字符可以是负号,但后面必须还有字符;除开头负号外,每个字符都得是数字。对着 solution.cpp 走:

```cpp
    char line[64];
    while (std::fgets(line, sizeof line, stdin)) {
        std::string s(line);
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
```

`fgets` 拿到的是「整行加换行」。换行不剥掉,`"42\n"` 里就混着一个非数字字符,42 永远进不了 int 那一支。剥的时候把 `\r` 顺手一起剥,是笔者给 Windows 风格的行尾留的余地。

判定就落在这几行:

```cpp
        bool digits = !s.empty();
        for (size_t i = 0; i < s.size(); ++i) {
            char ch = s[i];
            if (i == 0 && ch == '-' && s.size() > 1) continue;
            if (!std::isdigit((unsigned char)ch)) digits = false;
        }
```

`!s.empty()` 先把空行拦下:连第一个字符都没有,判不成整数。负号那行的条件有三个,您最容易漏的是 `s.size() > 1`。少了它,单独一个 `-` 会被当成合法负号,`atoi` 转出个 0,输出 `int 0`,判题要的可是 `text -`。`isdigit` 的参数转 `(unsigned char)` 也不是仪式:char 在这套环境里带符号,某些字节值转过去是负数,而 ctype 家族收到负数(除了 EOF)是未定义行为,这一步转换是规矩。

收尾分型:

```cpp
        if (digits) std::printf("int %d\n", std::atoi(s.c_str()));
        else std::printf("text %s\n", s.c_str());
```

咱们拿 `007` 走一遍 int 那一支:`atoi` 给 7,前导零在这条路上自然消化,输出 `int 7`。

**排错**:

还是不通过?症状最统一的一档是忘了剥换行:`isdigit` 见到 `'\n'` 就摇头,所有数字词全被判成 text,连 `hello` 这些 text 词也会带着多余的换行打出去,判题输出里行数直接翻倍。您要是看到全红,先查剥行尾那条 while。症状窄一点的,是负号判定漏了 `s.size() > 1`:五组用例只有 `-` 那一组挂,报出来的实际输出是 `int 0`,对着这条症状找,一步就定位。
