#include <chrono>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>
#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered_map.hpp>

// 实用工具：计时器
class Timer
{
private:
    std::chrono::high_resolution_clock::time_point start_time;
    std::string operation_name;

public:
    Timer(const std::string& name) : operation_name(name)
    {
        start_time = std::chrono::high_resolution_clock::now();
    }

    ~Timer()
    {
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
        std::cout << operation_name << ": " << duration << " ms" << std::endl;
    }

    double elapsed()
    {
        auto end_time = std::chrono::high_resolution_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
    }
};

// 测试数据生成器
class TestDataGenerator
{
private:
    std::mt19937 rng;
    std::uniform_int_distribution<int> dist;

public:
    TestDataGenerator(int seed = 42, int min = 0, int max = 1000000) : rng(seed), dist(min, max) {}

    std::vector<std::string> generateKeys(size_t count)
    {
        std::vector<std::string> keys;
        keys.reserve(count);
        for (size_t i = 0; i < count; ++i)
        {
            keys.push_back(std::to_string(dist(rng)));
        }
        return keys;
    }

    std::vector<std::string> generateValues(size_t count)
    {
        std::vector<std::string> values;
        values.reserve(count);
        for (size_t i = 0; i < count; ++i)
        {
            values.push_back("value-" + std::to_string(dist(rng)));
        }
        return values;
    }
};

// 基准测试类
template <typename MapType>
class SingleThreadBenchmark
{
private:
    MapType map;
    const size_t num_elements;
    const size_t num_operations;
    TestDataGenerator data_gen;

    std::vector<std::string> keys;
    std::vector<std::string> values;
    std::vector<std::string> lookup_keys;

    double testInsert()
    {
        Timer timer("插入性能");
        for (size_t i = 0; i < num_elements; ++i)
        {
            map.insert({keys[i], values[i]});
        }
        return timer.elapsed();
    }

    double testLookup()
    {
        Timer timer("查找性能");
        std::string result;
        size_t found_count = 0;
        volatile std::string dummy; // 防止编译器优化
        for (size_t i = 0; i < num_operations; ++i)
        {
            auto it = map.find(lookup_keys[i % lookup_keys.size()]);
            if (it != map.end())
            {
                result = it->second;
                const_cast<std::string&>(dummy) = result; // 使用结果防止被优化掉
                found_count++;
            }
        }
        std::cout << "找到: " << found_count << "/" << num_operations << std::endl;
        return timer.elapsed();
    }

    double testErase()
    {
        Timer timer("删除性能");
        size_t erased_count = 0;
        for (size_t i = 0; i < num_elements; ++i)
        {
            erased_count += map.erase(keys[i]);
        }
        std::cout << "删除: " << erased_count << "/" << num_elements << std::endl;
        return timer.elapsed();
    }

    // 测试负载因子对性能的影响
    void testLoadFactorImpact()
    {
        std::cout << "\n测试负载因子对性能的影响:" << std::endl;
        std::cout << "------------------------" << std::endl;

        const std::vector<double> load_factors = {0.1, 0.3, 0.5, 0.7, 0.8, 0.9, 0.95};
        const size_t test_size = 100000; // 使用较小的测试规模

        for (double load_factor : load_factors)
        {
            MapType test_map;
            const size_t bucket_count = static_cast<size_t>(test_size / load_factor);
            test_map.reserve(bucket_count);

            // 插入数据以达到目标负载因子
            Timer insert_timer("插入 (负载因子 = " + std::to_string(load_factor) + ")");
            for (size_t i = 0; i < test_size; ++i)
            {
                test_map.insert({keys[i], values[i]});
            }
            double insert_time = insert_timer.elapsed();

            // 测试查找性能
            Timer lookup_timer("查找 (负载因子 = " + std::to_string(load_factor) + ")");
            size_t found_count = 0;
            volatile std::string dummy; // 防止编译器优化
            for (size_t i = 0; i < test_size; ++i)
            {
                auto it = test_map.find(keys[i % test_size]);
                if (it != test_map.end())
                {
                    const_cast<std::string&>(dummy) = it->second; // 使用结果防止被优化掉
                    found_count++;
                }
            }
            double lookup_time = lookup_timer.elapsed();

            // 实际负载因子可能与目标不完全一致
            double actual_load_factor = static_cast<double>(test_map.size()) / test_map.bucket_count();
            std::cout << "目标负载因子: " << load_factor << ", 实际负载因子: " << actual_load_factor
                      << ", 插入时间: " << insert_time << " ms"
                      << ", 查找时间: " << lookup_time << " ms" << std::endl;
        }
    }

public:
    SingleThreadBenchmark(size_t elements = 1000000, size_t operations = 5000000)
        : num_elements(elements),
          num_operations(operations)
    {

        // 生成测试数据
        keys = data_gen.generateKeys(num_elements);
        values = data_gen.generateValues(num_elements);

        // 生成查找键（80%存在，20%不存在）
        lookup_keys = data_gen.generateKeys(num_operations);
        for (size_t i = 0; i < num_operations * 0.8; ++i)
        {
            lookup_keys[i] = keys[i % keys.size()];
        }
    }

    void runBenchmark(const std::string& map_name)
    {
        std::cout << "\n测试 " << map_name << " (单线程，元素: " << num_elements << ", 操作: " << num_operations << ")"
                  << std::endl;
        std::cout << "------------------------------------------------" << std::endl;

        // 预留空间以减少重新哈希的影响
        map.reserve(num_elements);

        // 运行测试
        double insert_time = testInsert();
        double lookup_time = testLookup();
        double erase_time = testErase();

        // 测试负载因子的影响（重新创建对象）
        testLoadFactorImpact();

        // 结果总结
        std::cout << "\n性能总结 (" << map_name << "):" << std::endl;
        std::cout << "插入: " << insert_time << " ms" << std::endl;
        std::cout << "查找: " << lookup_time << " ms" << std::endl;
        std::cout << "删除: " << erase_time << " ms" << std::endl;
        std::cout << "================================================" << std::endl;
    }
};

int main()
{
    // 设置测试参数
    const size_t num_elements = 1000000;   // 100万个元素
    const size_t num_operations = 5000000; // 500万次操作

    // 测试不同类型的map
    std::cout << "单线程基准测试 - 不使用互斥锁" << std::endl;
    std::cout << "=================================" << std::endl;

    SingleThreadBenchmark<std::unordered_map<std::string, std::string>> std_map_bench(num_elements, num_operations);
    std_map_bench.runBenchmark("std::unordered_map");

    SingleThreadBenchmark<boost::unordered_map<std::string, std::string>> boost_map_bench(num_elements, num_operations);
    boost_map_bench.runBenchmark("boost::unordered_map");

    SingleThreadBenchmark<boost::unordered_flat_map<std::string, std::string>> flat_map_bench(num_elements,
                                                                                              num_operations);
    flat_map_bench.runBenchmark("boost::unordered_flat_map");

    return 0;
}