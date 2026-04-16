// Timer.h
#pragma once
#include <unistd.h>
#include <deque>
#include <memory>
#include <queue>
#include "ChatSession.h"
#include "base/MutexLock.h"
#include "base/noncopyable.h"
#include "MemoryPool.h"   // 新增

class ChatSession;

class TimerNode {
public:
 TimerNode(std::shared_ptr<ChatSession> requestData, int timeout);
 ~TimerNode();
 TimerNode(TimerNode &tn);
 void update(int timeout);
 bool isValid();
 void clearReq();
 void setDeleted() { deleted_ = true; }
 bool isDeleted() const { return deleted_; }
 size_t getExpTime() const { return expiredTime_; }

 // 内存池相关：重载 operator new/delete
 static void* operator new(size_t size);
 static void operator delete(void* ptr);

private:
 bool deleted_;
 size_t expiredTime_;
 std::shared_ptr<ChatSession> SPHttpData;

 // 静态内存池实例
 static MemoryPool<TimerNode> pool_;
};

struct TimerCmp {
 bool operator()(std::shared_ptr<TimerNode> &a,
                 std::shared_ptr<TimerNode> &b) const {
  return a->getExpTime() > b->getExpTime();
 }
};

class TimerManager {
public:
 TimerManager();
 ~TimerManager();
 void addTimer(std::shared_ptr<ChatSession> SPHttpData, int timeout);
 void handleExpiredEvent();

private:
 typedef std::shared_ptr<TimerNode> SPTimerNode;
 std::priority_queue<SPTimerNode, std::deque<SPTimerNode>, TimerCmp>
     timerNodeQueue;
};