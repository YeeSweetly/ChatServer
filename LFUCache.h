//
// Created by 16657 on 2026/4/16.
//

// LFUCache.h
#pragma once

#include <unordered_map>
#include <list>
#include <memory>
#include <mutex>

template <typename Key, typename Value>
class LFUCache {
private:
    struct FreqNode;  // 前向声明

    struct DataNode {
        Key key;
        Value value;
        typename std::list<FreqNode>::iterator freqIter;  // 指向所属频次节点
        DataNode(const Key& k, const Value& v) : key(k), value(v) {}
    };

    struct FreqNode {
        int freq;
        std::list<DataNode> dataList;  // 该频次下的所有数据节点
        FreqNode(int f) : freq(f) {}
    };

    using FreqList = std::list<FreqNode>;
    using KeyMap = std::unordered_map<Key, typename std::list<DataNode>::iterator>;

    size_t capacity_;
    FreqList freqList_;               // 频次链表，按 freq 升序
    KeyMap keyMap_;                   // 键 -> 数据节点迭代器
    std::mutex mutex_;

    // 将数据节点移动到 freq+1 的频次节点中
    void promote(typename std::list<DataNode>::iterator dataIt) {
        auto freqIt = dataIt->freqIter;
        int newFreq = freqIt->freq + 1;
        auto nextFreqIt = std::next(freqIt);

        // 如果下一个频次节点不是 newFreq，则插入一个新节点
        if (nextFreqIt == freqList_.end() || nextFreqIt->freq != newFreq) {
            nextFreqIt = freqList_.emplace(nextFreqIt, newFreq);
        }

        // 将数据移动到新频次节点的 dataList 末尾
        nextFreqIt->dataList.splice(nextFreqIt->dataList.end(), freqIt->dataList, dataIt);
        dataIt->freqIter = nextFreqIt;

        // 如果原频次节点的 dataList 为空，则删除该频次节点
        if (freqIt->dataList.empty()) {
            freqList_.erase(freqIt);
        }
    }

public:
    explicit LFUCache(size_t capacity) : capacity_(capacity) {}

    // 获取值，若不存在返回默认构造的 Value（需判断）
    bool get(const Key& key, Value& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = keyMap_.find(key);
        if (it == keyMap_.end()) {
            return false;
        }
        auto dataIt = it->second;
        value = dataIt->value;
        promote(dataIt);
        return true;
    }

    // 插入或更新键值对
    void put(const Key& key, const Value& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = keyMap_.find(key);
        if (it != keyMap_.end()) {
            // 更新已有键
            auto dataIt = it->second;
            dataIt->value = value;
            promote(dataIt);
            return;
        }

        // 新键，需要淘汰最不常用的
        if (keyMap_.size() >= capacity_) {
            // 淘汰 freqList_ 第一个频次节点中的第一个数据节点
            auto& firstFreqNode = freqList_.front();
            auto& dataList = firstFreqNode.dataList;
            auto evictIt = dataList.begin();
            keyMap_.erase(evictIt->key);
            dataList.pop_front();
            if (dataList.empty()) {
                freqList_.pop_front();
            }
        }

        // 插入新数据，频次为 1
        if (freqList_.empty() || freqList_.front().freq != 1) {
            freqList_.emplace_front(1);
        }
        auto& freqOne = freqList_.front();
        freqOne.dataList.emplace_back(key, value);
        auto dataIt = std::prev(freqOne.dataList.end());
        dataIt->freqIter = freqList_.begin();
        keyMap_[key] = dataIt;
    }

    // 检查键是否存在
    bool contains(const Key& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        return keyMap_.find(key) != keyMap_.end();
    }

    // 移除某个键
    void erase(const Key& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = keyMap_.find(key);
        if (it == keyMap_.end()) return;
        auto dataIt = it->second;
        auto freqIt = dataIt->freqIter;
        freqIt->dataList.erase(dataIt);
        if (freqIt->dataList.empty()) {
            freqList_.erase(freqIt);
        }
        keyMap_.erase(it);
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return keyMap_.size();
    }
};