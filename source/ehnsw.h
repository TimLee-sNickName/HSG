#pragma once

#include <chrono>
#include <cinttypes>
#include <iostream>
#include <iterator>
#include <map>
#include <memory.h>
#include <memory>
#include <queue>
#include <set>
#include <stack>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "distance.h"

namespace ehnsw
{

// 向量
template <typename Dimension_Type> class Vector
{
  public:
    std::vector<Dimension_Type> data;

    explicit Vector(const std::vector<Dimension_Type> &data)
    {
        this->data = data;
    }
};

// 每层中的向量
class Vector_In_Index
{
  public:
    // 向量在原始数据的偏移量
    uint64_t global_offset;
    // 指向的邻居向量
    std::map<float, uint64_t> out;
    // 指向该向量的邻居向量
    std::unordered_set<uint64_t> in;

    explicit Vector_In_Index(const uint64_t global_offset)
    {
        this->global_offset = global_offset;
    }
};

// 索引中的一层
class Layer
{
  public:
    // 簇中的向量
    std::unordered_map<uint64_t, std::unique_ptr<Vector_In_Index>> vectors;
    // 该簇中被选出的向量在原始数据的偏移量
    std::unordered_set<uint64_t> selected_vectors;
    // 在插入向量的过程中
    // 如果一个新向量的插入导致其中某个邻居需要剪枝
    // 而该剪枝操作会导致图的不连通
    // 则不进行该剪枝操作，记录这条边
    // 在之后的向量插入完成后再检验这些边如果删去是否影响图的连通性
    //    std::unordered_map<uint64_t, std::weak_ptr<Vector_In_Cluster>> temporary_connecting;

    explicit Layer() = default;
};

// 索引
template <typename Dimension_Type> class Index
{
  public:
    // 向量原始数据
    std::vector<Vector<Dimension_Type>> vectors;
    // 自底向上存放每一层
    std::vector<std::unique_ptr<Layer>> layers;
    // 每个向量的最大邻居向量个数
    uint64_t minimum_connect_number{};
    // 距离计算
    float (*distance_calculation)(const std::vector<Dimension_Type> &vector1,
                                  const std::vector<Dimension_Type> &vector2);
    // 插入向量时候选邻居向量系数
    uint64_t relaxed_monotonicity{};
    uint64_t step{};

    explicit Index(const Distance_Type distance_type)
    {
        this->minimum_connect_number = 5;
        this->distance_calculation = get_distance_calculation_function<Dimension_Type>(distance_type);
        this->relaxed_monotonicity = 100;
        this->step = 3;
    }
};

namespace
{

bool connected(const std::unique_ptr<Layer> &layer, const uint64_t start,
               std::unordered_map<uint64_t, std::pair<float, uint64_t>> &deleted_edges, const uint64_t step)
{
    auto last = std::unordered_set<uint64_t>();
    auto next = std::unordered_set<uint64_t>();
    auto flag = std::unordered_set<uint64_t>();
    flag.insert(start);
    last.insert(start);
    for (auto round = 0; round < 4; ++round)
    {
        for (const auto &vector_global_offset : last)
        {
            for (const auto &neighbor : layer->vectors.find(vector_global_offset)->second->out)
            {
                auto neighbor_global_offset = neighbor.second;
                deleted_edges.erase(neighbor_global_offset);
                if (flag.insert(neighbor_global_offset).second)
                {
                    next.insert(neighbor_global_offset);
                }
            }
            for (const auto &neighbor_global_offset : layer->vectors.find(vector_global_offset)->second->in)
            {
                deleted_edges.erase(neighbor_global_offset);
                if (flag.insert(neighbor_global_offset).second)
                {
                    next.insert(neighbor_global_offset);
                }
            }
        }
        if (deleted_edges.empty())
        {
            return true;
        }
        std::swap(last, next);
        next.clear();
    }
    return false;
}

bool insert_to_upper_layer(const std::unique_ptr<Layer> &layer, const uint64_t vector_global_offset,
                           const uint64_t step)
{
    auto last = std::unordered_set<uint64_t>();
    auto next = std::unordered_set<uint64_t>();
    auto flag = std::unordered_set<uint64_t>();
    last.insert(vector_global_offset);
    for (auto round = 0; round < step; ++round)
    {
        for (const auto &last_vector_global_offset : last)
        {
            for (auto &neighbor : layer->vectors.find(last_vector_global_offset)->second->out)
            {
                auto neighbor_vector_global_offset = neighbor.second;
                if (flag.insert(neighbor_vector_global_offset).second)
                {
                    if (layer->selected_vectors.contains(neighbor_vector_global_offset))
                    {
                        return false;
                    }
                    next.insert(neighbor_vector_global_offset);
                }
            }
            for (auto &neighbor_vector_global_offset : layer->vectors.find(last_vector_global_offset)->second->in)
            {
                if (flag.insert(neighbor_vector_global_offset).second)
                {
                    if (layer->selected_vectors.contains(neighbor_vector_global_offset))
                    {
                        return false;
                    }
                    next.insert(neighbor_vector_global_offset);
                }
            }
        }
        std::swap(last, next);
        next.clear();
    }
    return true;
}

// 从开始向量查询开始向量所在簇中距离最近的top-k个向量
template <typename Dimension_Type>
std::map<float, uint64_t> nearest_neighbors(const Index<Dimension_Type> &index, const uint64_t layer_number,
                                            const std::vector<Dimension_Type> &query_vector, const uint64_t start,
                                            const uint64_t top_k, const uint64_t relaxed_monotonicity)
{
    // 优先队列
    auto nearest_neighbors = std::map<float, uint64_t>();
    if (index.layers[layer_number]->vectors.size() < top_k + relaxed_monotonicity)
    {
        for (auto &vector : index.layers[layer_number]->vectors)
        {
            nearest_neighbors.insert(std::make_pair(
                index.distance_calculation(query_vector, index.vectors[vector.first].data), vector.first));
        }
    }
    else
    {
        // 标记簇中的向量是否被遍历过
        std::unordered_set<uint64_t> flags;
        uint64_t out_of_bound = 0;
        // 排队队列
        auto waiting_vectors = std::map<float, uint64_t>();
        waiting_vectors.insert(
            std::make_pair(index.distance_calculation(query_vector, index.vectors[start].data), start));
        while (!waiting_vectors.empty())
        {
            auto processing_distance = waiting_vectors.begin()->first;
            auto processing_vector_global_offset = waiting_vectors.begin()->second;
            waiting_vectors.erase(waiting_vectors.begin());
            flags.insert(processing_vector_global_offset);
            // 如果已遍历的向量小于候选数量
            if (nearest_neighbors.size() < top_k)
            {
                nearest_neighbors.insert(std::make_pair(processing_distance, processing_vector_global_offset));
            }
            else
            {
                // 如果当前的向量和查询向量的距离小于已优先队列中的最大值
                if (nearest_neighbors.upper_bound(processing_distance) != nearest_neighbors.end())
                {
                    out_of_bound = 0;
                    nearest_neighbors.insert(std::make_pair(processing_distance, processing_vector_global_offset));
                    nearest_neighbors.erase(std::prev(nearest_neighbors.end()));
                }
                else if (relaxed_monotonicity < out_of_bound)
                {
                    break;
                }
                else
                {
                    ++out_of_bound;
                }
            }
            // 计算当前向量的出边指向的向量和目标向量的距离
            for (auto &vector : index.layers[layer_number]->vectors.find(processing_vector_global_offset)->second->out)
            {
                if (flags.insert(vector.second).second)
                {
                    waiting_vectors.insert(std::make_pair(
                        index.distance_calculation(query_vector, index.vectors[vector.second].data), vector.second));
                }
            }
            // 计算当前向量的入边指向的向量和目标向量的距离
            for (auto &vector_global_offset :
                 index.layers[layer_number]->vectors.find(processing_vector_global_offset)->second->in)
            {
                if (flags.insert(vector_global_offset).second)
                {
                    waiting_vectors.insert(std::make_pair(
                        index.distance_calculation(query_vector, index.vectors[vector_global_offset].data),
                        vector_global_offset));
                }
            }
        }
    }
    return nearest_neighbors;
}

template <typename Dimension_Type>
void add(Index<Dimension_Type> &index, const uint64_t new_vector_global_offset, uint64_t target_layer_number)
{
    // 记录被插入向量每一层中距离最近的max_connect个邻居向量
    auto every_layer_neighbors = std::stack<std::map<float, uint64_t>>();
    every_layer_neighbors.push(nearest_neighbors(
        index, index.layers.size() - 1, index.vectors[new_vector_global_offset].data,
        index.layers.back()->vectors.begin()->first, index.minimum_connect_number, index.relaxed_monotonicity));
    // 逐层扫描
    // 因为Vector_InCluster中每个向量记录了自己在下层中对应的向量
    // 所以不需要实际的层和簇
    // 直接通过上一层中返回的结果即可进行计算
    for (int64_t layer_number = index.layers.size() - 2; 0 <= layer_number; --layer_number)
    {
        every_layer_neighbors.push(nearest_neighbors(index, layer_number, index.vectors[new_vector_global_offset].data,
                                                     every_layer_neighbors.top().begin()->second,
                                                     index.minimum_connect_number, index.relaxed_monotonicity));
    }
    // 插入向量
    while (!every_layer_neighbors.empty())
    {
        auto temporary_new_vector = std::make_unique<Vector_In_Index>(new_vector_global_offset);
        index.layers[target_layer_number]->vectors.insert(
            std::make_pair(new_vector_global_offset, std::move(temporary_new_vector)));
        const auto &new_vector = index.layers[target_layer_number]->vectors.find(new_vector_global_offset)->second;
        auto deleted_edges = std::unordered_map<uint64_t, std::pair<float, uint64_t>>();
        for (const auto &neighbor : every_layer_neighbors.top())
        {
            const auto &neighbor_vector = index.layers[target_layer_number]->vectors.find(neighbor.second)->second;
            // 将新的向量指向邻居向量
            new_vector->out.insert(neighbor);
            // 在邻居向量中记录指向自己的新向量
            neighbor_vector->in.insert(new_vector_global_offset);
            // 新向量和邻居向量的距离小于邻居向量已指向的10个向量的距离
            if (neighbor_vector->out.upper_bound(neighbor.first) != neighbor_vector->out.end())
            {
                neighbor_vector->out.insert(std::make_pair(neighbor.first, new_vector_global_offset));
                new_vector->in.insert(neighbor.second);
            }
            // 如果邻居向量的出度大于最大出度数量
            if (index.minimum_connect_number < neighbor_vector->out.size())
            {
                auto temporary = neighbor_vector->out.begin();
                std::advance(temporary, index.minimum_connect_number);
                auto record = *temporary;
                deleted_edges.insert(std::make_pair(record.second, std::make_pair(record.first, neighbor.second)));
                neighbor_vector->out.erase(temporary);
                index.layers[target_layer_number]->vectors.find(record.second)->second->in.erase(neighbor.second);
            }
        }
        if (!connected(index.layers[target_layer_number], new_vector_global_offset, deleted_edges, index.step))
        {
            for (const auto &edge : deleted_edges)
            {
                index.layers[target_layer_number]
                    ->vectors.find(edge.second.second)
                    ->second->out.insert(std::make_pair(edge.second.first, edge.first));
                index.layers[target_layer_number]->vectors.find(edge.first)->second->in.insert(edge.second.second);
            }
        }
        // 如果新向量应该被插入上一层中
        if (insert_to_upper_layer(index.layers[target_layer_number], new_vector_global_offset, index.step))
        {
            every_layer_neighbors.pop();
            index.layers[target_layer_number]->selected_vectors.insert(new_vector_global_offset);
            ++target_layer_number;
            if (every_layer_neighbors.empty())
            {
                index.layers.push_back(std::make_unique<Layer>());
                auto temporary = std::make_unique<Vector_In_Index>(new_vector_global_offset);
                index.layers.back()->vectors.insert(std::make_pair(new_vector_global_offset, std::move(temporary)));
                break;
            }
        }
        else
        {
            break;
        }
    }
}

} // namespace

// 查询
template <typename Dimension_Type>
std::map<float, uint64_t> query(const Index<Dimension_Type> &index, const std::vector<Dimension_Type> &query_vector,
                                uint64_t top_k, uint64_t relaxed_monotonicity = 0)
{
    if (index.vectors.empty())
    {
        throw std::logic_error("Empty vectors in index. ");
    }
    if (query_vector.size() != index.vectors[0].data.size())
    {
        throw std::invalid_argument("The dimension of query vector is not "
                                    "equality with vectors in index. ");
    }
    if (relaxed_monotonicity == 0)
    {
        relaxed_monotonicity = top_k / 2;
    }
    std::map<float, uint64_t> result;
    // 如果索引中的向量数量小于top-k
    // 直接暴力搜索返回排序后的全部结果
    if (index.vectors.size() < top_k)
    {
        for (uint64_t global_offset = 0; global_offset < index.vectors.size(); ++global_offset)
        {
            result.insert(std::make_pair(index.distance_calculation(query_vector, index.vectors[global_offset].data),
                                         global_offset));
        }
    }
    else
    {
        if (index.layers.size() == 1)
        {
            result = nearest_neighbors(index, 0, query_vector, index.layers[0]->vectors.begin()->first, top_k,
                                       relaxed_monotonicity);
        }
        else
        {
            // 记录被插入向量每一层中距离最近的top_k个邻居向量
            result = nearest_neighbors(index, index.layers.size() - 1, query_vector,
                                       index.layers.back()->vectors.begin()->first, 1, index.relaxed_monotonicity);
            // 逐层扫描
            for (uint64_t i = index.layers.size() - 2; 0 < i; --i)
            {
                result =
                    nearest_neighbors(index, i, query_vector, result.begin()->second, 1, index.relaxed_monotonicity);
            }
            result = nearest_neighbors(index, 0, query_vector, result.begin()->second, top_k, relaxed_monotonicity);
        }
    }
    return result;
}

// 插入
template <typename Dimension_Type>
void insert(Index<Dimension_Type> &index, const std::vector<Dimension_Type> &inserted_vector)
{
    // 插入向量在原始数据中的偏移量
    uint64_t inserted_vector_global_offset = index.vectors.size();
    // 如果是空的索引
    if (inserted_vector_global_offset == 0)
    {
        index.vectors.push_back(Vector<Dimension_Type>(inserted_vector));
        index.layers.push_back(std::make_unique<Layer>());
        auto new_vector = std::make_unique<Vector_In_Index>(inserted_vector_global_offset);
        index.layers.back()->vectors.insert(std::make_pair(inserted_vector_global_offset, std::move(new_vector)));
        return;
    }
    // 如果插入向量的维度不等于索引里向量的维度
    if (inserted_vector.size() != index.vectors[0].data.size())
    {
        throw std::invalid_argument("The dimension of insert vector is not "
                                    "equality with vectors in index. ");
    }
    auto temporary = std::to_string(inserted_vector_global_offset);
    switch (temporary.length())
    {
    case 5:
        index.minimum_connect_number = 6;
        //        index.relaxed_monotonicity = 5;
        break;
    case 6:
        index.minimum_connect_number = 7;
        //        index.relaxed_monotonicity = 20;
        break;
    case 7:
        index.minimum_connect_number = 8;
        //        index.relaxed_monotonicity = 30;
        break;
    case 8:
        index.minimum_connect_number = 9;
        //        index.relaxed_monotonicity = 40;
        break;
    case 9:
        index.minimum_connect_number = 10;
        //        index.relaxed_monotonicity = 50;
        break;
    case 10:
        index.minimum_connect_number = 11;
        //        index.relaxed_monotonicity = 60;
        break;
    }
    if (5 < temporary.size())
    {
        switch (temporary[1])
        {
        case '0':
        case '1':
            index.step = 6;
            break;
        case '2':
        case '3':
            index.step = 5;
            break;
        case '4':
        case '5':
            index.step = 4;
            break;
        case '6':
        case '7':
            index.step = 3;
            break;
        case '8':
        case '9':
            index.step = 2;
            break;
        }
    }
    index.vectors.push_back(Vector<Dimension_Type>(inserted_vector));
    add(index, inserted_vector_global_offset, 0);
}

} // namespace ehnsw
