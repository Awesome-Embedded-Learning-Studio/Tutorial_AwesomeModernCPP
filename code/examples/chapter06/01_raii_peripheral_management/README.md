# Chapter 6.1: RAII 在外设管理中的应用

本目录包含 RAII (Resource Acquisition Is Initialization) 在嵌入式外设管理中的应用示例。

## 文件说明

| 文件 | 描述 |
|------|------|
| `gpio_raii.cpp` | GPIO 引脚 RAII 管理示例 |
| `spi_transaction.cpp` | SPI 事务自动片选管理示例 |
| `dma_channel.cpp` | DMA 通道资源管理示例 |
| `scope_guard.cpp` | 通用作用域守卫实现示例 |

## 关键概念

- **RAII**: 资源在构造时获取，在析构时释放
- **不可拷贝**: RAII 资源对象通常不可拷贝，只能移动
- **自动清理**: 无论正常退出还是异常/提前返回，资源都会被正确释放
- **嵌入式友好**: 避免在析构函数中进行阻塞操作

## 编译运行

```bash
mkdir build && cd build
cmake ..
make
./gpio_raii
./spi_transaction
./dma_channel
./scope_guard
```
