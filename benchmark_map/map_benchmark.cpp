#include <atomic>
#include <chrono>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <random>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <boost/unordered/concurrent_flat_map.hpp>
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

// 一个接口统一的包装器，便于操作不同类型的map
template <typename MapType>
class MapWrapper
{
protected:
    MapType map;
    mutable std::shared_mutex rwmtx;

public:
    virtual void insert(const typename MapType::key_type& key, const typename MapType::mapped_type& value)
    {
        std::unique_lock<std::shared_mutex> lock(rwmtx); // 写锁
        map.insert({key, value});
    }

    virtual bool find(const typename MapType::key_type& key, typename MapType::mapped_type& value)
    {
        if constexpr (
            std::is_same_v<
                MapType,
                boost::unordered::concurrent_flat_map<typename MapType::key_type, typename MapType::mapped_type>>)
        {
            bool found = false;
            map.cvisit(key,
                       [&value, &found](const auto& kv)
                       {
                           value = kv.second;
                           found = true;
                       });
            return found;
        }
        else
        {
            std::shared_lock<std::shared_mutex> lock(rwmtx); // 读锁
            auto it = map.find(key);
            if (it != map.end())
            {
                value = it->second;
                return true;
            }
        }
        return false;
    }

    virtual void erase(const typename MapType::key_type& key)
    {
        std::unique_lock<std::shared_mutex> lock(rwmtx); // 写锁
        map.erase(key);
    }

    virtual size_t size()
    {
        std::shared_lock<std::shared_mutex> lock(rwmtx); // 读锁
        return map.size();
    }

    virtual void clear()
    {
        std::unique_lock<std::shared_mutex> lock(rwmtx); // 写锁
        map.clear();
    }
};

// 针对 boost::concurrent_flat_map 的特殊实现
template <typename K, typename V>
class ConcurrentMapWrapper : public MapWrapper<boost::concurrent_flat_map<K, V>>
{
public:
    using Base = MapWrapper<boost::concurrent_flat_map<K, V>>;

    void insert(const K& key, const V& value) override
    {
        Base::map.insert({key, value});
    }

    bool find(const K& key, V& value) override
    {
        bool found = false;
        Base::map.cvisit(key,
                         [&value, &found](const std::pair<const K, V>& kv)
                         {
                             value = kv.second;
                             found = true;
                         });
        return found;
    }

    void erase(const K& key) override
    {
        Base::map.erase(key);
    }

    size_t size() override
    {
        return Base::map.size();
    }

    void clear() override
    {
        Base::map.clear();
    }
};

// 基准测试
class Benchmark
{
private:
    const size_t num_elements;
    const size_t num_operations;
    const size_t num_threads;
    TestDataGenerator data_gen;

    std::vector<std::string> keys;
    std::vector<std::string> values;
    std::vector<std::string> lookup_keys;

    // 防止编译器优化掉查找结果的变量
    std::atomic<size_t> result_checksum{0};

    template <typename MapWrapper>
    double testSingleThreadInsert(MapWrapper& map_wrapper)
    {
        Timer timer("单线程插入");
        for (size_t i = 0; i < num_elements; ++i)
        {
            map_wrapper.insert(keys[i], values[i]);
        }
        return timer.elapsed();
    }

    template <typename MapWrapper>
    double testSingleThreadLookup(MapWrapper& map_wrapper)
    {
        Timer timer("单线程查找");
        std::string result;
        size_t found_count = 0;
        size_t checksum = 0;
        volatile std::string volatile_result;
        for (size_t i = 0; i < num_operations; ++i)
        {
            if (map_wrapper.find(lookup_keys[i % lookup_keys.size()], result))
            {
                found_count++;
                // 使用结果计算校验和，防止编译器优化
                checksum += result.length();
                const_cast<std::string&>(volatile_result) = result;
            }
        }
        result_checksum += checksum;
        std::cout << "找到: " << found_count << "/" << num_operations << " (校验和: " << checksum << ")" << std::endl;
        return timer.elapsed();
    }

    template <typename MapWrapper>
    double testMultiThreadedInsert(MapWrapper& map_wrapper)
    {
        Timer timer("多线程插入");
        // 预先排序所有待插入键，确保所有map类型按相同顺序插入
        std::vector<size_t> indices(num_elements);
        for (size_t i = 0; i < num_elements; ++i)
        {
            indices[i] = i;
        }
        std::sort(indices.begin(), indices.end(), [&](size_t a, size_t b) { return keys[a] < keys[b]; });
        // 按顺序为线程分配工作
        std::vector<std::thread> threads;
        size_t chunk_size = indices.size() / num_threads;

        auto worker = [&](size_t start, size_t end)
        {
            for (size_t i = start; i < end; ++i)
            {
                size_t idx = indices[i];
                map_wrapper.insert(keys[idx], values[idx]);
            }
        };

        for (size_t t = 0; t < num_threads; ++t)
        {
            size_t start = t * chunk_size;
            size_t end = (t == num_threads - 1) ? indices.size() : (t + 1) * chunk_size;
            threads.emplace_back(worker, start, end);
        }

        for (auto& thread : threads)
        {
            thread.join();
        }

        return timer.elapsed();
    }

    template <typename MapWrapper>
    double testMultiThreadedLookup(MapWrapper& map_wrapper)
    {
        Timer timer("多线程查找");
        std::vector<std::thread> threads;
        std::atomic<size_t> found_count{0};
        std::vector<size_t> thread_checksums(num_threads, 0); // 每个线程独立记录校验和

        auto worker = [&](size_t thread_id, size_t start, size_t end)
        {
            std::string result;
            size_t local_found = 0;
            size_t local_checksum = 0;

            // 确保每个线程处理的查询顺序一致
            for (size_t i = start; i < end; ++i)
            {
                size_t idx = i % lookup_keys.size();
                if (map_wrapper.find(lookup_keys[idx], result))
                {
                    local_found++;
                    local_checksum += result.length();
                }
            }

            found_count += local_found;
            thread_checksums[thread_id] = local_checksum; // 存储到对应位置
        };

        size_t chunk_size = num_operations / num_threads;
        for (size_t t = 0; t < num_threads; ++t)
        {
            size_t start = t * chunk_size;
            size_t end = (t == num_threads - 1) ? num_operations : (t + 1) * chunk_size;
            threads.emplace_back(worker, t, start, end);
        }

        for (auto& thread : threads)
        {
            thread.join();
        }

        // 按顺序累加各线程的校验和，避免原子操作的顺序不确定性
        size_t total_checksum = 0;
        for (size_t t = 0; t < num_threads; ++t)
        {
            total_checksum += thread_checksums[t];
        }

        result_checksum += total_checksum;
        std::cout << "找到: " << found_count << "/" << num_operations << " (校验和: " << total_checksum << ")"
                  << std::endl;
        return timer.elapsed();
    }

    template <typename MapWrapper>
    double testMixedOperations(MapWrapper& map_wrapper)
    {
        Timer timer("多线程混合读写");
        std::vector<std::thread> threads;
        std::atomic<size_t> found_count{0};
        std::atomic<size_t> insert_count{0};
        std::vector<size_t> thread_checksums(num_threads, 0);

        auto worker = [&](size_t id)
        {
            std::string result;
            size_t local_found = 0;
            size_t local_insert = 0;
            size_t local_checksum = 0;
            std::mt19937 local_rng(42 + id); // 使用固定种子
            std::uniform_int_distribution<int> op_dist(0, 9);

            // 预先生成操作序列，确保每次运行相同
            std::vector<int> operations;
            operations.reserve(num_operations / num_threads);
            for (size_t i = 0; i < num_operations / num_threads; ++i)
            {
                operations.push_back(op_dist(local_rng));
            }

            for (size_t i = 0; i < num_operations / num_threads; ++i)
            {
                int op = operations[i];
                int idx = i % keys.size();

                if (op < 7)
                { // 查找
                    if (map_wrapper.find(lookup_keys[idx], result))
                    {
                        local_found++;
                        local_checksum += result.length();
                    }
                }
                else if (op < 9)
                { // 插入
                    map_wrapper.insert(keys[idx], values[idx]);
                    local_insert++;
                }
                else
                { // 删除
                    map_wrapper.erase(lookup_keys[idx]);
                }
            }

            found_count += local_found;
            insert_count += local_insert;
            thread_checksums[id] = local_checksum;
        };

        for (size_t t = 0; t < num_threads; ++t)
        {
            threads.emplace_back(worker, t);
        }

        for (auto& thread : threads)
        {
            thread.join();
        }

        // 按顺序累加校验和
        size_t total_checksum = 0;
        for (size_t t = 0; t < num_threads; ++t)
        {
            total_checksum += thread_checksums[t];
        }

        result_checksum += total_checksum;
        std::cout << "找到: " << found_count << ", 插入: " << insert_count << " (校验和: " << total_checksum << ")"
                  << std::endl;
        return timer.elapsed();
    }

public:
    Benchmark(size_t elements = 1000000, size_t operations = 1000000, size_t threads = 4)
        : num_elements(elements),
          num_operations(operations),
          num_threads(threads)
    {
        std::cout << "num_elements: " << num_elements << ", num_operations: " << num_operations
                  << ", num_threads: " << num_threads << std::endl;
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

    template <typename MapWrapper>
    void runBenchmark(const std::string& map_name)
    {
        std::cout << "\n测试 " << map_name << " (元素: " << num_elements << ", 操作: " << num_operations
                  << ", 线程: " << num_threads << ")" << std::endl;
        std::cout << "------------------------------------------------" << std::endl;

        auto map = std::make_unique<MapWrapper>();

        // 单线程测试
        double insert_time = testSingleThreadInsert(*map);
        double lookup_time = testSingleThreadLookup(*map);

        // 清空并重新测试
        map->clear();

        // 多线程测试
        double mt_insert_time = testMultiThreadedInsert(*map);
        double mt_lookup_time = testMultiThreadedLookup(*map);

        // 清空并测试混合操作
        map->clear();
        map->insert(keys[0], values[0]); // 确保map不为空
        double mixed_time = testMixedOperations(*map);

        // 结果总结
        std::cout << "\n性能总结 (" << map_name << "):" << std::endl;
        std::cout << "单线程插入: " << insert_time << " ms" << std::endl;
        std::cout << "单线程查找: " << lookup_time << " ms" << std::endl;
        std::cout << "多线程插入: " << mt_insert_time << " ms" << std::endl;
        std::cout << "多线程查找: " << mt_lookup_time << " ms" << std::endl;
        std::cout << "多线程混合: " << mixed_time << " ms" << std::endl;
        std::cout << "总校验和: " << result_checksum.load() << std::endl;
        std::cout << "================================================" << std::endl;
    }
};

int main()
{
    // 设置测试参数
    const size_t num_elements = 1000000;   // 100万个元素
    const size_t num_operations = 5000000; // 500万次操作
    const size_t num_threads = 8;          // 8个线程

    Benchmark benchmark(num_elements, num_operations, num_threads);

    // 测试不同类型的map
    benchmark.runBenchmark<MapWrapper<std::unordered_map<std::string, std::string>>>("std::unordered_map");
    benchmark.runBenchmark<MapWrapper<boost::unordered_map<std::string, std::string>>>("boost::unordered_map");
    benchmark.runBenchmark<MapWrapper<boost::unordered_flat_map<std::string, std::string>>>(
        "boost::unordered_flat_map");
    benchmark.runBenchmark<ConcurrentMapWrapper<std::string, std::string>>("boost::concurrent_flat_map");

    return 0;
}