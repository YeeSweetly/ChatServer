// Timer.cpp
#include "Timer.h"
#include <sys/time.h>
#include <unistd.h>
#include <queue>

// 静态内存池定义
MemoryPool<TimerNode> TimerNode::pool_;

void* TimerNode::operator new(size_t size) {
  return pool_.allocate();
}

void TimerNode::operator delete(void* ptr) {
  pool_.deallocate(ptr);
}

TimerNode::TimerNode(std::shared_ptr<ChatSession> requestData, int timeout)
    : deleted_(false), SPHttpData(requestData) {
  struct timeval now;
  gettimeofday(&now, NULL);
  expiredTime_ = (((now.tv_sec % 10000) * 1000) + (now.tv_usec / 1000)) + timeout;
}

TimerNode::~TimerNode() {
  if (SPHttpData) SPHttpData->handleClose();
}

TimerNode::TimerNode(TimerNode &tn)
    : SPHttpData(tn.SPHttpData), expiredTime_(0) {}

void TimerNode::update(int timeout) {
  struct timeval now;
  gettimeofday(&now, NULL);
  expiredTime_ = (((now.tv_sec % 10000) * 1000) + (now.tv_usec / 1000)) + timeout;
}

bool TimerNode::isValid() {
  struct timeval now;
  gettimeofday(&now, NULL);
  size_t temp = (((now.tv_sec % 10000) * 1000) + (now.tv_usec / 1000));
  if (temp < expiredTime_)
    return true;
  else {
    this->setDeleted();
    return false;
  }
}

void TimerNode::clearReq() {
  SPHttpData.reset();
  this->setDeleted();
}

TimerManager::TimerManager() {}
TimerManager::~TimerManager() {}

void TimerManager::addTimer(std::shared_ptr<ChatSession> SPHttpData, int timeout) {
  SPTimerNode new_node(new TimerNode(SPHttpData, timeout));
  timerNodeQueue.push(new_node);
  SPHttpData->linkTimer(new_node);
}

void TimerManager::handleExpiredEvent() {
  while (!timerNodeQueue.empty()) {
    SPTimerNode ptimer_now = timerNodeQueue.top();
    if (ptimer_now->isDeleted())
      timerNodeQueue.pop();
    else if (ptimer_now->isValid() == false)
      timerNodeQueue.pop();
    else
      break;
  }
}