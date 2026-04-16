//
// Created by 16657 on 2026/4/16.
//

// MemoryPool.h
#pragma once

#include <cstddef>
#include <vector>
#include <mutex>
#include <memory>
#include <type_traits>

template <typename T, size_t BlockSize = 1024>
class MemoryPool {
    static_assert(!std::is_polymorphic<T>::value,
                  "MemoryPool does not support polymorphic types with virtual destructor");

private:
    union Slot {
        T element;
        Slot* next;
        Slot() {}  // 不初始化 element
        ~Slot() {} // 不调用 element 的析构函数
    };

    // 内存块，每个块包含 BlockSize 个 Slot
    struct Block {
        Slot slots[BlockSize];
        Block* next;
        Block() : next(nullptr) {}
    };

    Block* blockHead_;          // 内存块链表头
    Slot* freeHead_;            // 空闲槽位链表头
    size_t allocatedCount_;     // 已分配的对象数量
    std::mutex mutex_;          // 线程安全锁

    // 分配一个新的内存块
    void allocateBlock() {
        Block* newBlock = new Block();
        newBlock->next = blockHead_;
        blockHead_ = newBlock;

        // 将新块中的所有 Slot 插入空闲链表
        for (size_t i = 0; i < BlockSize; ++i) {
            Slot* slot = &newBlock->slots[i];
            slot->next = freeHead_;
            freeHead_ = slot;
        }
    }

public:
    MemoryPool() : blockHead_(nullptr), freeHead_(nullptr), allocatedCount_(0) {}

    ~MemoryPool() {
        std::lock_guard<std::mutex> lock(mutex_);
        // 释放所有内存块
        Block* block = blockHead_;
        while (block) {
            Block* next = block->next;
            delete block;
            block = next;
        }
    }

    // 分配一个 T 类型对象的内存，但不构造对象
    void* allocate() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!freeHead_) {
            allocateBlock();
        }
        Slot* slot = freeHead_;
        freeHead_ = slot->next;
        ++allocatedCount_;
        return static_cast<void*>(slot);
    }

    // 释放内存，不调用析构函数
    void deallocate(void* ptr) {
        if (!ptr) return;
        std::lock_guard<std::mutex> lock(mutex_);
        Slot* slot = static_cast<Slot*>(ptr);
        slot->next = freeHead_;
        freeHead_ = slot;
        --allocatedCount_;
    }

    // 构造对象并返回指针（placement new）
    template <typename... Args>
    T* construct(Args&&... args) {
        void* mem = allocate();
        return new (mem) T(std::forward<Args>(args)...);
    }

    // 析构对象并归还内存
    void destroy(T* ptr) {
        if (ptr) {
            ptr->~T();
            deallocate(ptr);
        }
    }
};

// 使用内存池的自定义分配器，可用于 STL 容器或 std::allocate_shared
template <typename T, typename Pool>
class PoolAllocator {
public:
    using value_type = T;
    using pointer = T*;
    using const_pointer = const T*;
    using reference = T&;
    using const_reference = const T&;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;

    template <typename U> struct rebind { using other = PoolAllocator<U, Pool>; };

    PoolAllocator(Pool& pool) : pool_(&pool) {}
    template <typename U>
    PoolAllocator(const PoolAllocator<U, Pool>& other) : pool_(other.pool_) {}

    pointer allocate(size_type n) {
        if (n == 1) {
            return static_cast<pointer>(pool_->allocate());
        }
        // 当需要分配多个对象时，回退到全局 operator new
        return static_cast<pointer>(::operator new(n * sizeof(T)));
    }

    void deallocate(pointer p, size_type n) {
        if (n == 1) {
            pool_->deallocate(p);
        } else {
            ::operator delete(p);
        }
    }

    template <typename U, typename... Args>
    void construct(U* p, Args&&... args) {
        ::new ((void*)p) U(std::forward<Args>(args)...);
    }

    template <typename U>
    void destroy(U* p) {
        p->~U();
    }

    bool operator==(const PoolAllocator& other) const { return pool_ == other.pool_; }
    bool operator!=(const PoolAllocator& other) const { return !(*this == other); }

    Pool* pool_;
};