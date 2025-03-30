#!/bin/bash

# 创建构建目录
mkdir -p build
cd build

# 编译
echo "编译基准测试程序..."
cmake ..
make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

# 运行基准测试
echo ""
echo "====================================================="
echo "运行单线程基准测试..."
echo "====================================================="
./single_thread_benchmark | tee ../single_thread_results.txt

echo ""
echo "====================================================="
echo "运行多线程基准测试..."
echo "====================================================="
./map_benchmark | tee ../multi_thread_results.txt

echo ""
echo "基准测试完成！结果已保存到 single_thread_results.txt 和 multi_thread_results.txt" 