# 哈希表性能基准测试

该项目用于测试不同哈希表实现在单线程和多线程环境下的性能表现。

测试的哈希表实现包括：
- `std::unordered_map`：C++标准库哈希表
- `boost::unordered_map`：Boost传统哈希表实现
- `boost::unordered_flat_map`：Boost基于开放寻址的现代哈希表
- `boost::concurrent_flat_map`：Boost支持并发操作的哈希表

## 环境要求

- C++17兼容的编译器
- CMake 3.10或更高版本
- Boost 1.87.0或更高版本（默认路径：`/opt/homebrew/Cellar/boost/1.87.0_1/include`）

## 编译方法

```bash
mkdir build
cd build
cmake ..
make
```

## 运行方法

```bash
./map_benchmark
```

## 测试项目

1. 单线程性能
   - 插入性能
   - 查找性能

2. 多线程性能
   - 并发插入性能
   - 并发查找性能
   - 混合读写性能（70%读、20%写、10%删除）

## 测试数据

- 默认测试数据量：100万个键值对
- 默认操作次数：500万次
- 默认线程数：8线程

## 注意事项

- 本测试使用互斥锁为非并发安全的哈希表提供并发保护
- 测试结果可能因硬件、编译器和操作系统而异 