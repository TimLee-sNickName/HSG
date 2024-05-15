#pragma once

#include <map>
#include <queue>
#include <random>
#include <stack>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "space.h"

namespace HSG
{

    // 向量
    class Vector
    {
      public:
        // 向量的外部id
        uint64_t id;
        //
        uint64_t offset;
        // 向量的数据
        const float *data;
        // 短的出边
        std::multimap<float, uint64_t> short_edge_out;
        // 短的入边
        std::unordered_set<uint64_t> short_edge_in;
        // 长的出边
        std::multimap<float, uint64_t> long_edge_out;
        // 长的入边
        std::unordered_map<uint64_t, float> long_edge_in;
        //
        std::unordered_set<uint64_t> keep_connected;

        explicit Vector(const uint64_t id, uint64_t offset, const float *const data_address)
            : id(id), offset(offset), data(data_address)
        {
        }
    };

    class Index_Parameters
    {
      public:
        // 向量的维度
        uint64_t dimension;
        // 距离类型
        Space::Metric space_metric;
        //
        uint64_t magnification;
        // 插入向量时提前终止条件
        // termination_number = short_edge_lower_limit + magnification
        uint64_t termination_number;
        // 短边数量下限
        uint64_t short_edge_lower_limit;
        // 短边数量上限
        uint64_t short_edge_upper_limit;
        // 覆盖范围
        uint64_t cover_range;

        explicit Index_Parameters(const uint64_t dimension, const Space::Metric space_metric,
                                  const uint64_t magnification, const uint64_t short_edge_lower_limit,
                                  const uint64_t cover_range)
            : dimension(dimension), space_metric(space_metric), magnification(magnification),
              termination_number(short_edge_lower_limit + magnification),
              short_edge_lower_limit(short_edge_lower_limit), cover_range(cover_range)
        {
        }
    };

    // 索引
    // 索引使用零点作为默认起始点
    // 使用64位无符号整形的最大值作为零点的id
    // 所以向量的id应大于等于0且小于64位无符号整形的最大值
    class Index
    {
      public:
        // 索引的参数
        Index_Parameters parameters;
        // 距离计算
        float (*similarity)(const float *vector1, const float *vector2, uint64_t dimension);
        // 索引中向量的数量
        uint64_t count;
        // 索引中的向量
        std::vector<Vector> vectors;
        //
        std::stack<uint64_t> empty;
        //
        std::vector<float> zero;
        //
        std::unordered_map<uint64_t, uint64_t> id_to_offset;

        explicit Index(const Space::Metric space, const uint64_t dimension, const uint64_t short_edge_lower_limit,
                       const uint64_t magnification, const uint64_t cover_range)
            : parameters(dimension, space, magnification, short_edge_lower_limit, cover_range), count(1),
              similarity(Space::get_similarity(space)), zero(dimension, 0.0)
        {
            this->vectors.push_back(Vector(std::numeric_limits<uint64_t>::max(), 0, this->zero.data()));
            this->id_to_offset.insert({std::numeric_limits<uint64_t>::max(), 0});
        }
    };

    inline int64_t get_offset(const Index &index, const uint64_t id)
    {
        return index.id_to_offset.find(id)->second;
    }

    // 查询距离目标向量最近的k个向量
    // k = index.parameters.short_edge_lower_limit
    // 返回最近邻和不属于最近邻但是在路径上的顶点
    void search_add(const Index &index, const float *const target_vector,
                    std::vector<std::pair<float, uint64_t>> &long_path,
                    std::vector<std::pair<float, uint64_t>> &short_path,
                    std::priority_queue<std::pair<float, uint64_t>> &nearest_neighbors)
    {
        // 等待队列
        auto waiting_vectors =
            std::priority_queue<std::pair<float, uint64_t>, std::vector<std::pair<float, uint64_t>>, std::greater<>>();
        waiting_vectors.push({Space::Euclidean2::distance_to_zero(target_vector, index.parameters.dimension), 0});

        // 标记是否被遍历过
        auto visited = std::vector<bool>(index.vectors.size(), false);
        visited[0] = true;

        // 阶段一：
        // 利用长边快速找到定位到处于目标向量附近区域的向量
        while (true)
        {
            auto &processing_offset = waiting_vectors.top().second;
            auto &processing_vector = index.vectors[processing_offset];
            long_path.push_back(waiting_vectors.top());

            for (auto iterator = processing_vector.long_edge_out.begin();
                 iterator != processing_vector.long_edge_out.end(); ++iterator)
            {
                auto &neighbor_offset = iterator->second;

                // 计算当前向量的出边指向的向量和目标向量的距离
                if (!visited[neighbor_offset])
                {
                    visited[neighbor_offset] = true;
                    waiting_vectors.push({index.similarity(target_vector, index.vectors[neighbor_offset].data,
                                                           index.parameters.dimension),
                                          neighbor_offset});
                }
            }

            if (processing_offset == waiting_vectors.top().second)
            {
                break;
            }
        }

        // 阶段二：
        // 利用短边找到和目标向量最近的向量
        while (true)
        {
            auto &processing_offset = waiting_vectors.top().second;
            auto &processing_vector = index.vectors[processing_offset];

            for (auto iterator = processing_vector.short_edge_out.begin();
                 iterator != processing_vector.short_edge_out.end(); ++iterator)
            {
                auto &neighbor_offset = iterator->second;

                // 计算当前向量的出边指向的向量和目标向量的距离
                if (!visited[neighbor_offset])
                {
                    visited[neighbor_offset] = true;
                    waiting_vectors.push({index.similarity(target_vector, index.vectors[neighbor_offset].data,
                                                           index.parameters.dimension),
                                          neighbor_offset});
                }
            }

            for (auto iterator = processing_vector.short_edge_in.begin();
                 iterator != processing_vector.short_edge_in.end(); ++iterator)
            {
                auto &neighbor_offset = *iterator;

                // 计算当前向量的出边指向的向量和目标向量的距离
                if (!visited[neighbor_offset])
                {
                    visited[neighbor_offset] = true;
                    waiting_vectors.push({index.similarity(target_vector, index.vectors[neighbor_offset].data,
                                                           index.parameters.dimension),
                                          neighbor_offset});
                }
            }

            for (auto iterator = processing_vector.keep_connected.begin();
                 iterator != processing_vector.keep_connected.end(); ++iterator)
            {
                auto &neighbor_offset = *iterator;

                // 计算当前向量的出边指向的向量和目标向量的距离
                if (!visited[neighbor_offset])
                {
                    visited[neighbor_offset] = true;
                    waiting_vectors.push({index.similarity(target_vector, index.vectors[neighbor_offset].data,
                                                           index.parameters.dimension),
                                          neighbor_offset});
                }
            }

            if (processing_offset == waiting_vectors.top().second)
            {
                break;
            }

            short_path.push_back(waiting_vectors.top());
        }

        // 阶段三：
        // 查找与目标向量相似度最高（距离最近）的k个向量
        while (!waiting_vectors.empty())
        {
            auto &processing_distance = waiting_vectors.top().first;
            auto &processing_offset = waiting_vectors.top().second;
            auto &processing_vector = index.vectors[processing_offset];
            waiting_vectors.pop();

            // 如果优先队列中的向量的数量小于k
            if (nearest_neighbors.size() < index.parameters.termination_number)
            {
                nearest_neighbors.push({processing_distance, processing_offset});
            }
            else
            {
                // 如果当前的向量和查询向量的距离小于优先队列中的最大值
                if (processing_distance < nearest_neighbors.top().first)
                {
                    nearest_neighbors.pop();
                    nearest_neighbors.push({processing_distance, processing_offset});
                }
                else
                {
                    break;
                }
            }

            for (auto iterator = processing_vector.short_edge_out.begin();
                 iterator != processing_vector.short_edge_out.end(); ++iterator)
            {
                auto &neighbor_offset = iterator->second;

                // 计算当前向量的出边指向的向量和目标向量的距离
                if (!visited[neighbor_offset])
                {
                    visited[neighbor_offset] = true;
                    waiting_vectors.push({index.similarity(target_vector, index.vectors[neighbor_offset].data,
                                                           index.parameters.dimension),
                                          neighbor_offset});
                }
            }

            for (auto iterator = processing_vector.short_edge_in.begin();
                 iterator != processing_vector.short_edge_in.end(); ++iterator)
            {
                auto &neighbor_offset = *iterator;

                // 计算当前向量的出边指向的向量和目标向量的距离
                if (!visited[neighbor_offset])
                {
                    visited[neighbor_offset] = true;
                    waiting_vectors.push({index.similarity(target_vector, index.vectors[neighbor_offset].data,
                                                           index.parameters.dimension),
                                          neighbor_offset});
                }
            }

            for (auto iterator = processing_vector.keep_connected.begin();
                 iterator != processing_vector.keep_connected.end(); ++iterator)
            {
                auto &neighbor_offset = *iterator;
                // 计算当前向量的出边指向的向量和目标向量的距离
                if (!visited[neighbor_offset])
                {
                    visited[neighbor_offset] = true;
                    waiting_vectors.push({index.similarity(target_vector, index.vectors[neighbor_offset].data,
                                                           index.parameters.dimension),
                                          neighbor_offset});
                }
            }
        }
    }

    bool connected(const Index &index, const Vector &vector, uint64_t offset)
    {
        auto visited = std::vector<bool>(index.vectors.size(), false);
        visited[vector.offset] = true;
        auto last = std::unordered_set<uint64_t>();
        last.insert(vector.offset);
        auto next = std::unordered_set<uint64_t>();

        for (auto round = 0; round < 4; ++round)
        {
            for (auto iterator = last.begin(); iterator != last.end(); ++iterator)
            {
                auto &t = index.vectors[*iterator];

                for (auto iterator = t.short_edge_in.begin(); iterator != t.short_edge_in.end(); ++iterator)
                {
                    auto &t1 = *iterator;

                    if (!visited[t1])
                    {
                        visited[t1] = true;
                        next.insert(t1);
                    }
                }

                for (auto iterator = t.short_edge_out.begin(); iterator != t.short_edge_out.end(); ++iterator)
                {
                    auto &t1 = iterator->second;

                    if (!visited[t1])
                    {
                        visited[t1] = true;
                        next.insert(t1);
                    }
                }

                for (auto iterator = t.keep_connected.begin(); iterator != t.keep_connected.end(); ++iterator)
                {
                    auto &t1 = *iterator;

                    if (!visited[t1])
                    {
                        visited[t1] = true;
                        next.insert(t1);
                    }
                }
            }
        }

        if (visited[offset])
        {
            return true;
        }

        return false;
    }

    // 添加
    void add(Index &index, const uint64_t id, const float *const added_vector_data)
    {
        auto offset = uint64_t(index.count);
        // 索引中向量数量加一
        ++index.count;

        if (index.empty.empty())
        {
            // 在索引中创建一个新向量
            index.vectors.push_back(Vector(id, offset, added_vector_data));
        }
        else
        {
            offset = index.empty.top();
            index.empty.pop();
            index.vectors[offset].id = id;
            index.vectors[offset].data = added_vector_data;
        }

        index.id_to_offset.insert({id, offset});
        auto &new_vector = index.vectors[offset];
        // 搜索距离新增向量最近的index.parameters.short_edge_lower_limit个向量
        // 同时记录搜索过程中遇到的向量
        auto nearest_neighbors = std::priority_queue<std::pair<float, uint64_t>>();
        auto long_path = std::vector<std::pair<float, uint64_t>>();
        auto short_path = std::vector<std::pair<float, uint64_t>>();
        search_add(index, added_vector_data, long_path, short_path, nearest_neighbors);

        // 添加短边
        while (!nearest_neighbors.empty())
        {
            auto distance = nearest_neighbors.top().first;
            auto &neighbor = index.vectors[nearest_neighbors.top().second];
            nearest_neighbors.pop();

            // 为新向量添加出边
            new_vector.short_edge_out.insert({distance, neighbor.offset});

            // 为邻居向量添加入边
            neighbor.short_edge_in.insert(offset);

            // 如果邻居向量的出边小于短边下限
            if (neighbor.short_edge_out.size() < index.parameters.short_edge_lower_limit)
            {
                // 邻居向量添加出边
                neighbor.short_edge_out.insert({distance, offset});
                // 新向量添加入边
                new_vector.short_edge_in.insert(neighbor.offset);
            }
            // 如果新向量距离邻居的距离小于邻居当前距离最大的出边的距离
            else if (distance < neighbor.short_edge_out.rbegin()->first)
            {
                auto farest_distance = neighbor.short_edge_out.rbegin()->first;
                auto farest_offset = neighbor.short_edge_out.rbegin()->second;

                // 邻居向量删除距离最大的出边
                neighbor.short_edge_out.erase(std::prev(neighbor.short_edge_out.end()));

                auto &temporary = index.vectors[farest_offset];
                temporary.short_edge_in.erase(neighbor.offset);

                if (!neighbor.short_edge_in.contains(farest_offset) && !connected(index, neighbor, farest_offset))
                {
                    if (neighbor.short_edge_out.size() < index.parameters.short_edge_upper_limit)
                    {
                        neighbor.short_edge_out.insert({farest_distance, farest_offset});
                    }
                    else
                    {
                        neighbor.keep_connected.insert(farest_offset);
                        temporary.keep_connected.insert(neighbor.offset);
                    }
                }

                // 邻居向量添加出边
                neighbor.short_edge_out.insert({distance, offset});
                // 新向量添加入边
                new_vector.short_edge_in.insert(neighbor.offset);
            }
        }

        // TODO 添加长边
        // 添加长边
        if (index.parameters.cover_range <= short_path.size())
        {
        }
    }

    // TODO 删除
    // 删除索引中的向量
    // void erase(Index &index, const uint64_t removed_vector_id)
    // {
    //     auto &removed_vector = index.vectors.find(removed_vector_id)->second;
    //     // 删除短边的出边
    //     for (auto iterator = removed_vector.short_edge_out.begin(); iterator != removed_vector.short_edge_out.end();
    //          ++iterator)
    //     {
    //         auto &neighbor_id = iterator->second;
    //         index.vectors.find(neighbor_id)->second.short_edge_in.erase(removed_vector_id);
    //     }
    //     // 删除短边的入边
    //     for (auto iterator = removed_vector.short_edge_in.begin(); iterator != removed_vector.short_edge_in.end();
    //          ++iterator)
    //     {
    //         auto &neighbor_id = iterator->first;
    //         auto &distance = iterator->second;
    //         auto &vector = index.vectors.find(neighbor_id)->second;
    //         // 删除边
    //         auto temporary = vector.short_edge_out.find(distance);
    //         while (temporary->second != removed_vector_id)
    //         {
    //             ++temporary;
    //         }
    //         vector.short_edge_out.erase(temporary);
    //     }
    //     // 补一条边
    //     for (auto iterator = removed_vector.short_edge_in.begin(); iterator != removed_vector.short_edge_in.end();
    //          ++iterator)
    //     {
    //         auto &repaired_id = iterator->first;
    //         // 需要补边的向量
    //         auto &repaired_vector = index.vectors.find(repaired_id)->second;
    //         {
    //             // 记录被遍历过得向量
    //             auto visited = std::vector<bool>(index.vectors.size(), false);
    //             // 优先队列
    //             auto nearest_neighbors = std::priority_queue<std::pair<float, uint64_t>>();
    //             // 排队队列
    //             auto waiting_vectors = std::priority_queue<std::pair<float, uint64_t>,
    //                                                        std::vector<std::pair<float, uint64_t>>,
    //                                                        std::greater<>>();
    //             for (auto temporary = repaired_vector.short_edge_out.begin();
    //                  temporary != repaired_vector.short_edge_out.end(); ++iterator)
    //             {
    //                 waiting_vectors.push(*temporary);
    //                 visited.insert(temporary->second);
    //             }
    //             while (!waiting_vectors.empty())
    //             {
    //                 auto processing_distance = waiting_vectors.top().first;
    //                 auto processing_vector_id = waiting_vectors.top().second;
    //                 auto &processing_vector = index.vectors.find(processing_vector_id)->second;
    //                 waiting_vectors.pop();
    //                 // 如果已遍历的向量小于候选数量
    //                 if (nearest_neighbors.size() < index.parameters.magnification)
    //                 {
    //                     nearest_neighbors.push({processing_distance, processing_vector_id});
    //                 }
    //                 else
    //                 {
    //                     // 如果当前的向量和查询向量的距离小于已优先队列中的最大值
    //                     if (processing_distance < nearest_neighbors.top().first)
    //                     {
    //                         nearest_neighbors.pop();
    //                         nearest_neighbors.push({processing_distance, processing_vector_id});
    //                     }
    //                     else
    //                     {
    //                         break;
    //                     }
    //                 }
    //                 for (auto iterator = processing_vector.short_edge_out.begin();
    //                      iterator != processing_vector.short_edge_out.end(); ++iterator)
    //                 {
    //                     auto neighbor_id = iterator->second;
    //                     // 计算当前向量的出边指向的向量和目标向量的距离
    //                     if (!visited.contains(neighbor_id))
    //                     {
    //                         visited.insert(neighbor_id);
    //                         waiting_vectors.push(
    //                             {index.similarity(repaired_vector.data.data(),
    //                                                         index.vectors.find(neighbor_id)->second.data.data(),
    //                                                         index.parameters.dimension),
    //                              neighbor_id});
    //                     }
    //                 }
    //                 for (auto iterator = processing_vector.short_edge_in.begin();
    //                      iterator != processing_vector.short_edge_in.end(); ++iterator)
    //                 {
    //                     auto neighbor_id = iterator->first;
    //                     // 计算当前向量的出边指向的向量和目标向量的距离
    //                     if (!visited.contains(neighbor_id))
    //                     {
    //                         visited.insert(neighbor_id);
    //                         waiting_vectors.push(
    //                             {index.similarity(repaired_vector.data.data(),
    //                                                         index.vectors.find(neighbor_id)->second.data.data(),
    //                                                         index.parameters.dimension),
    //                              neighbor_id});
    //                     }
    //                 }
    //                 for (auto iterator = processing_vector.keep_connected.begin();
    //                      iterator != processing_vector.keep_connected.end(); ++iterator)
    //                 {
    //                     auto neighbor_id = *iterator;
    //                     // 计算当前向量的出边指向的向量和目标向量的距离
    //                     if (!visited.contains(neighbor_id))
    //                     {
    //                         visited.insert(neighbor_id);
    //                         waiting_vectors.push(
    //                             {index.similarity(repaired_vector.data.data(),
    //                                                         index.vectors.find(neighbor_id)->second.data.data(),
    //                                                         index.parameters.dimension),
    //                              neighbor_id});
    //                     }
    //                 }
    //             }
    //             while (nearest_neighbors.size() != 1)
    //             {
    //                 nearest_neighbors.pop();
    //             }
    //             repaired_vector.short_edge_out.insert(nearest_neighbors.top());
    //             index.vectors.find(nearest_neighbors.top().second)
    //                 ->second.short_edge_in.insert({repaired_id, nearest_neighbors.top().first});
    //         }
    //     }
    //     // 处理长边
    //     // 删除出边
    //     for (auto iterator = removed_vector.long_edge_out.begin(); iterator != removed_vector.long_edge_out.end();
    //          ++iterator)
    //     {
    //         auto &neighbor_id = iterator->second;
    //         index.vectors.find(neighbor_id)->second.long_edge_in.erase(removed_vector_id);
    //     }
    //     // 计算分母
    //     float sum = 0;
    //     for (auto iterator = removed_vector.long_edge_in.begin(); iterator != removed_vector.long_edge_in.end();
    //     ++iterator)
    //     {
    //         auto &distance = iterator->second;
    //         sum += distance;
    //     }
    //     // 删除入边并补边
    //     // 真随机数生成器
    //     std::random_device random_device_generator;
    //     // 伪随机数生成器
    //     // 用真随机数生成器初始化
    //     std::mt19937 mt19937_generator(random_device_generator());
    //     // 均匀分布
    //     std::uniform_real_distribution<float> distribution(0, sum);
    //     for (auto iterator = removed_vector.long_edge_in.begin(); iterator != removed_vector.long_edge_in.end();
    //     ++iterator)
    //     {
    //         auto &repaired_id = iterator->first;
    //         auto &in_edge_distance = iterator->second;
    //         auto &repaired_vector = index.vectors.find(repaired_id)->second;
    //         // 删除边
    //         {
    //             auto iterator = repaired_vector.long_edge_out.find(in_edge_distance);
    //             while (iterator->second != removed_vector_id)
    //             {
    //                 ++iterator;
    //             }
    //             repaired_vector.long_edge_out.erase(iterator);
    //         }
    //         // 补边
    //         for (auto iterator = removed_vector.long_edge_out.begin(); iterator !=
    //         removed_vector.long_edge_out.end();
    //              ++iterator)
    //         {
    //             auto &new_long_edge_id = iterator->second;
    //             if (distribution(mt19937_generator) < in_edge_distance)
    //             {
    //                 auto &new_long_edge = index.vectors.find(new_long_edge_id)->second;
    //                 auto distance = index.similarity(repaired_vector.data.data(), new_long_edge.data.data(),
    //                                                            index.parameters.dimension);
    //                 if (!prune(index, repaired_vector, new_long_edge, distance))
    //                 {
    //                     repaired_vector.long_edge_out.insert({distance, new_long_edge_id});
    //                     new_long_edge.long_edge_in.insert({repaired_id, distance});
    //                 }
    //             }
    //         }
    //     }
    //     index.vectors.erase(removed_vector_id);
    // }

    // TODO 先获取要计算的向量再统一计算
    // TODO 预取向量数据
    // 查询距离目标向量最近的top-k个向量
    std::priority_queue<std::pair<float, uint64_t>> nearest_neighbors_search(const Index &index,
                                                                             const float *const target_vector,
                                                                             const uint64_t top_k,
                                                                             const uint64_t magnification)
    {
        // 优先队列
        auto nearest_neighbors = std::priority_queue<std::pair<float, uint64_t>>();
        // 标记是否被遍历过
        auto visited = std::vector<bool>(index.vectors.size(), false);
        visited[0] = true;
        // 排队队列
        auto waiting_vectors =
            std::priority_queue<std::pair<float, uint64_t>, std::vector<std::pair<float, uint64_t>>, std::greater<>>();
        auto &zero_vector = index.vectors[0];

        for (auto iterator = zero_vector.long_edge_out.begin(); iterator != zero_vector.long_edge_out.end(); ++iterator)
        {
            auto &neighbor_offset = iterator->second;
            visited[neighbor_offset] = true;
            waiting_vectors.push(
                {index.similarity(target_vector, index.vectors[neighbor_offset].data, index.parameters.dimension),
                 neighbor_offset});
        }

        // 阶段一：
        // 利用长边快速找到定位到处于目标向量附件区域的向量
        while (true)
        {
            auto &nearest_offset = waiting_vectors.top().second;
            auto &nearest_vector = index.vectors[nearest_offset];

            for (auto iterator = nearest_vector.long_edge_out.begin(); iterator != nearest_vector.long_edge_out.end();
                 ++iterator)
            {
                auto &neighbor_offset = iterator->second;

                // 计算当前向量的出边指向的向量和目标向量的距离
                if (!visited[neighbor_offset])
                {
                    visited[neighbor_offset] = true;
                    waiting_vectors.push({index.similarity(target_vector, index.vectors[neighbor_offset].data,
                                                           index.parameters.dimension),
                                          neighbor_offset});
                }
            }

            if (waiting_vectors.top().second == nearest_offset)
            {
                break;
            }
        }

        // 阶段二：
        // 查找与目标向量相似度最高（距离最近）的top-k个向量
        while (!waiting_vectors.empty())
        {
            auto processing_distance = waiting_vectors.top().first;
            auto processing_vector_offset = waiting_vectors.top().second;
            waiting_vectors.pop();
            auto &processing_vector = index.vectors[processing_vector_offset];

            // 如果已遍历的向量小于候选数量
            if (nearest_neighbors.size() < top_k + magnification)
            {
                nearest_neighbors.push({processing_distance, processing_vector.id});
            }
            else
            {
                // 如果当前的向量和查询向量的距离小于已优先队列中的最大值
                if (processing_distance < nearest_neighbors.top().first)
                {
                    nearest_neighbors.pop();
                    nearest_neighbors.push({processing_distance, processing_vector.id});
                }
                else
                {
                    break;
                }
            }

            for (auto iterator = processing_vector.short_edge_out.begin();
                 iterator != processing_vector.short_edge_out.end(); ++iterator)
            {
                auto &neighbor_offset = iterator->second;

                // 计算当前向量的出边指向的向量和目标向量的距离
                if (!visited[neighbor_offset])
                {
                    visited[neighbor_offset] = true;
                    waiting_vectors.push({index.similarity(target_vector, index.vectors[neighbor_offset].data,
                                                           index.parameters.dimension),
                                          neighbor_offset});
                }
            }

            for (auto iterator = processing_vector.short_edge_in.begin();
                 iterator != processing_vector.short_edge_in.end(); ++iterator)
            {
                auto &neighbor_offset = *iterator;

                // 计算当前向量的出边指向的向量和目标向量的距离
                if (!visited[neighbor_offset])
                {
                    visited[neighbor_offset] = true;
                    waiting_vectors.push({index.similarity(target_vector, index.vectors[neighbor_offset].data,
                                                           index.parameters.dimension),
                                          neighbor_offset});
                }
            }

            for (auto iterator = processing_vector.keep_connected.begin();
                 iterator != processing_vector.keep_connected.end(); ++iterator)
            {
                auto &neighbor_offset = *iterator;

                // 计算当前向量的出边指向的向量和目标向量的距离
                if (!visited[neighbor_offset])
                {
                    visited[neighbor_offset] = true;
                    waiting_vectors.push({index.similarity(target_vector, index.vectors[neighbor_offset].data,
                                                           index.parameters.dimension),
                                          neighbor_offset});
                }
            }
        }

        return nearest_neighbors;
    }

    // 查询
    std::priority_queue<std::pair<float, uint64_t>> search(const Index &index, const float *const query_vector,
                                                           const uint64_t top_k, const uint64_t magnification = 0)
    {
        // if (index.count == 0)
        // {
        //     throw std::logic_error("Index is empty. ");
        // }
        // if (query_vector.size() != index.parameters.dimension)
        // {
        //     throw std::invalid_argument("The dimension of target vector is not "
        //                                 "equality with vectors in index. ");
        // }
        return nearest_neighbors_search(index, query_vector, top_k, magnification);
    }

    // TODO 计算覆盖率
    // 计算覆盖率

    // TODO 优化索引结构
    // 优化索引结构

} // namespace HSG
