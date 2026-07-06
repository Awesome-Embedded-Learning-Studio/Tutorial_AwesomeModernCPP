"""共享的 frontmatter 标签白名单(单一数据源)。

被 validate_frontmatter.py 与 check_quality.py 共同 import。
补 tag 只改本文件,两个校验脚本自动同步——历史上有过「两套白名单不同步
导致 Content Quality CI 挂」的坑,本模块就是为了根治它。
"""

VALID_TAGS = {
    # Concepts
    'RAII', '移动语义', '零开销抽象', '编译期计算', '类型安全',
    '内存管理', '异步编程', '模板元编程',

    # Language features
    'constexpr', 'consteval', 'constinit', 'lambda', 'CRTP',
    'concepts', 'coroutine', 'if_constexpr', '模板', '泛型',

    # Patterns
    '对象池', '状态机', '工厂模式', '策略模式', '单例模式',
    '观察者模式', 'RAII守卫', '回调机制',
    # GoF 设计模式(创造型 / 结构型 / 行为型)
    '构建器模式', '原型模式', '适配器模式', '桥接模式', '组合模式',
    '装饰器模式', '外观模式', '享元模式', '代理模式', '命令模式',
    '备忘录模式', '访问者模式', '迭代器模式', '责任链模式',
    '解释器模式', '中介者模式', '模板方法模式',

    # Containers
    'array', 'span', 'vector', 'map', 'unordered_map',
    '循环缓冲区', '侵入式容器', '容器',

    # Smart pointers
    'unique_ptr', 'shared_ptr', 'weak_ptr', 'intrusive_ptr',
    '智能指针', '引用计数',

    # Type safety
    'enum', 'enum_class', 'variant', 'optional', 'expected',
    '类型别名', '字面量',

    # Functional
    '函数对象', 'std_function', 'std_invoke', 'Ranges',

    # Concurrency
    'atomic', 'memory_order', 'mutex', '无锁', '并发',

    # Embedded specific
    '嵌入式', '单片机', '外设管理', '寄存器', '链接器',
    '交叉编译', '工具链', 'CMake',

    # General
    '基础', '入门', '进阶', '实战', '优化', '工程实践',

    # Domains / engineering practices (notebook-migration batch)
    '网络编程', '内存安全', '调试', '测试',

    # Platforms
    'host', 'stm32f1',

    # Audience / difficulty (as tags)
    'beginner', 'intermediate', 'advanced', 'cpp-modern',
}
