// ThreadPool.h
#pragma once
#include <pthread.h>
#include <functional>
#include <memory>
#include <vector>
#include <queue>
#include "base/MutexLock.h"
#include "base/Condition.h"
#include "base/noncopyable.h"

class ThreadPool : noncopyable {
public:
    typedef std::function<void()> Task;

    ThreadPool(int threadCount, int queueSize);
    ~ThreadPool();

    bool start();
    bool addTask(Task&& task);
    void stop();

private:
    static void* workerThread(void* arg);
    void runInThread();

    int threadCount_;
    int queueSize_;
    bool running_;
    std::vector<pthread_t> threads_;
    std::queue<Task> taskQueue_;
    MutexLock mutex_;
    Condition notEmpty_;
    Condition notFull_;
};