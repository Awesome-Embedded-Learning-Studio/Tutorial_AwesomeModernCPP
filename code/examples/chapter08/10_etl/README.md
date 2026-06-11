# Chapter 7.5 - ETL（Embedded Template Library）示例

本目录包含《ETL（Embedded Template Library）》章节的所有代码示例。

## 说明

ETL 是一个面向嵌入式的 C++ 模板库，提供了许多类似 STL 的容器与工具，但所有容器都是**固定容量**或**最大容量**的 —— 不会调用 `malloc/new`。

## 为什么使用 ETL

- **确定性**：不再担心 heap 碎片、分配失败或无法预测的延迟
- **性能与缓存友好**：容器的存储通常是连续分配的，遍历时更亲近 CPU 缓存
- **STL 风格 API**：保留熟悉的接口，对程序员友好
- **MIT 开源**：在 GitHub 上活跃维护

## 获取 ETL

ETL 可以从 GitHub 获取：https://github.com/ETLCPP/etl

## 示例文件

| 文件 | 说明 | 关键点 |
|------|------|--------|
| `etl_vector_demo.cpp` | 固定容量向量 | 编译期确定容量，无堆分配 |
| `etl_queue_demo.cpp` | 固定容量队列 | 适合消息传递、中断处理 |
| `etl_pool_demo.cpp` | 对象池 | 预分配对象，避免碎片 |

## 编译方法

```bash
# 首先获取 ETL 库
git clone https://github.com/ETLCPP/etl.git

# 修改 CMakeLists.txt 中的 include 路径指向 ETL
# 或设置环境变量
export ETL_PATH=/path/to/etl/include

mkdir build && cd build
cmake -DETL_INCLUDE_PATH=$ETL_PATH ..
make
```

## 注意事项

1. ETL 库需要单独安装或作为子模块添加到项目中
2. 这些示例需要包含 ETL 头文件才能编译
3. ETL 兼容 C++03 及更高版本
4. 在嵌入式环境中，推荐使用 ETL 而不是 STL 来避免动态分配

## 相关链接

- ETL 官方文档：https://www.etlcpp.com/
- GitHub 仓库：https://github.com/ETLCPP/etl
