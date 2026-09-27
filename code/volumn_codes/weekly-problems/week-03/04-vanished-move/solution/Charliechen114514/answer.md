**思路**:

病根在第 9 行——那个手写的析构函数。

听起来不挨着:析构函数管收尾,怎么会牵连移动构造?规则是 C++11 定下的:一个类只要**用户声明**了析构函数(或者任何一个拷贝、移动操作),编译器就不再隐式生成移动构造和移动赋值。理由很朴素:您都亲手写了资源怎么释放,编译器就不再替您决定资源怎么转移。这条规则单看冷僻,放在这题里正好踩中。

于是第 19 行 `RecordingBuffer b = std::move(a);` 实际发生的是:`std::move(a)` 只是把 a 转成右值;重载决议在构造函数里挑——移动构造,不存在;拷贝构造,还在(它的隐式生成在 C++11 里被标成了过时,但仍然有效)。右值可以绑定 `const RecordingBuffer&`,于是拷贝构造被选中,做的是浅拷贝:`a.data_` 和 `b.data_` 指向同一块内存。本地实测取证,拷贝完成后两个 `data()` 一字不差。

结尾就顺理成章:main 退出,a、b 依次析构,同一块内存 `delete[]` 两遍——double free。

clang 其实把话说透了,开 `-Wdeprecated-copy-with-user-provided-dtor` 就能看到:

```text
warning: definition of implicit copy constructor for 'RecordingBuffer' is
deprecated because it has a user-provided destructor
note: in implicit copy constructor for 'RecordingBuffer' first required here
    RecordingBuffer b = std::move(a);
```

注意 note 点名的行:编译器明说了,这一步用的是 **implicit copy constructor**。

怎么修?Core Guidelines C.21 一句话:「拷贝、移动、析构这一族函数,要么都别碰,要么一起定义或一起删除」(rule of five)。落到这个类:

```cpp
RecordingBuffer(const RecordingBuffer&) = delete;
RecordingBuffer& operator=(const RecordingBuffer&) = delete;

RecordingBuffer(RecordingBuffer&& other) noexcept : data_(other.data_) {
    other.data_ = nullptr;
}

RecordingBuffer& operator=(RecordingBuffer&& other) noexcept {
    if (this != &other) {
        delete[] data_;
        data_ = other.data_;
        other.data_ = nullptr;
    }
    return *this;
}
```

移动构造把指针接过来、把源指针置空——析构照跑两遍,第二遍 `delete[] nullptr`,安全。修完实测:`a.data()` 为空指针,程序干净退场。更省事的修法是把裸指针换成 `std::vector<int>` 或 `std::unique_ptr<int[]>`,五个特殊成员一个都不用写(rule of zero),让成员自己管自己——新代码里这是首选。

**排错**:

- 病根标到第 19 行:那里只是出错的位置。题面已经把「move 没生效」讲在前面,要找的是「谁让它没生效」——把视线从使用处挪到类的声明上。
- 删掉第 9 行了事:确实不崩了,但裸指针从此无人释放,内存泄漏,RAII 也没了——崩溃没了,问题换成了泄漏,没解决。
