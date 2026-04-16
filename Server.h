// Server.h
#pragma once

#include <memory>
#include <unordered_map>
#include "Channel.h"
#include "EventLoop.h"
#include "EventLoopThreadPool.h"
#include "ChatSession.h"
#include "LFUCache.h"               // 新增

class Server {
public:
 Server(EventLoop *loop, int threadNum, int port);
 ~Server() {}

 EventLoop *getLoop() const { return loop_; }
 void start();
 void handNewConn();
 void handThisConn() { loop_->updatePoller(acceptChannel_); }

 void addSession(SP_ChatSession session);
 void removeSession(SP_ChatSession session);
 void broadcastMessage(const std::string& msg, SP_ChatSession exclude);

 // 新增：获取历史消息（先查 LFU 缓存，再查 Redis）
 std::vector<std::string> getRecentHistory(const std::string& roomId, int count);

private:
 EventLoop *loop_;
 int threadNum_;
 std::unique_ptr<EventLoopThreadPool> eventLoopThreadPool_;
 bool started_;
 std::shared_ptr<Channel> acceptChannel_;
 int port_;
 int listenFd_;
 static const int MAXFDS = 100000;

 std::unordered_map<int, SP_ChatSession> sessions_;
 MutexLock mutex_;

 // LFU 缓存：缓存房间最近 N 条历史消息，键格式 "roomId:count"
 std::unique_ptr<LFUCache<std::string, std::vector<std::string>>> historyCache_;
};