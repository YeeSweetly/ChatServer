// ThreadPool.cpp
#include "ThreadPool.h"
#include <assert.h>
#include "base/Logging.h"

ThreadPool::ThreadPool(int threadCount, int queueSize)
    : threadCount_(threadCount),
      queueSize_(queueSize),
      running_(false),
      mutex_(),
      notEmpty_(mutex_),
      notFull_(mutex_) {
    if (threadCount_ <= 0) threadCount_ = 4;
    if (queueSize_ <= 0) queueSize_ = 1024;
}

ThreadPool::~ThreadPool() {
    if (running_) stop();
}

bool ThreadPool::start() {
    running_ = true;
    threads_.resize(threadCount_);
    for (int i = 0; i < threadCount_; ++i) {
        if (pthread_create(&threads_[i], nullptr, workerThread, this) != 0) {
            running_ = false;
            return false;
        }
    }
    return true;
}

void ThreadPool::stop() {
    {
        MutexLockGuard lock(mutex_);
        running_ = false;
        notEmpty_.notifyAll();
        notFull_.notifyAll();
    }
    for (auto& tid : threads_) {
        pthread_join(tid, nullptr);
    }
}

// 改为 try-enqueue，满了就直接丢弃（历史写入丢失可以接受，不能阻塞 EventLoop）：
bool ThreadPool::addTask(Task&& task) {
    MutexLockGuard lock(mutex_);
    if (!running_ || taskQueue_.size() >= static_cast<size_t>(queueSize_)) {
        return false;   // 满了直接返回，不阻塞
    }
    taskQueue_.push(std::move(task));
    notEmpty_.notify();
    return true;
}

void* ThreadPool::workerThread(void* arg) {
    ThreadPool* pool = static_cast<ThreadPool*>(arg);
    pool->runInThread();
    return nullptr;
}

void ThreadPool::runInThread() {
    while (running_) {
        Task task;
        {
            MutexLockGuard lock(mutex_);
            while (running_ && taskQueue_.empty()) {
                notEmpty_.wait();
            }
            if (!running_ && taskQueue_.empty()) return;
            task = std::move(taskQueue_.front());
            taskQueue_.pop();
            notFull_.notify();
        }
        if (task) task();
    }
}