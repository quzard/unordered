cmake_minimum_required(VERSION 3.10)
project(map_benchmark)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 添加boost路径
set(BOOST_ROOT /opt/homebrew/Cellar/boost/1.87.0_1)
include_directories(${BOOST_ROOT}/include)
link_directories(${BOOST_ROOT}/lib)

# 添加编译选项
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=native")

# 添加可执行文件
add_executable(map_benchmark map_benchmark.cpp)
add_executable(single_thread_benchmark single_thread_benchmark.cpp)

# 添加线程库
find_package(Threads REQUIRED)
target_link_libraries(map_benchmark Threads::Threads)
target_link_libraries(single_thread_benchmark Threads::Threads)

# 安装
install(TARGETS map_benchmark single_thread_benchmark DESTINATION bin) 