// Server.h  —  修复版
// 修复点：
//   1. 新增 redis_pool_ 异步线程池，saveMessage 不再阻塞 EventLoop
//   2. 新增 outBufferLimit 常量，超限时关闭慢客户端
#pragma once

#include <memory>
#include <unordered_map>
#include "Channel.h"
#include "EventLoop.h"
#include "EventLoopThreadPool.h"
#include "ChatSession.h"
#include "LFUCache.h"
#include "ThreadPool.h"

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

    // 广播：exclude 为 nullptr 时发给所有人（含发送者）
    // 压测场景下发送者也需要收到回包，以便统计延迟
    void broadcastMessage(const std::string& msg, SP_ChatSession exclude);

    // 异步写 Redis，不阻塞 EventLoop 线程
    void asyncSaveMessage(const std::string& roomId,
                          const std::string& sender,
                          const std::string& message);

    std::vector<std::string> getRecentHistory(const std::string& roomId, int count);

    // outBuffer 上限：超过此值时强制关闭连接，防止内存无限增长
    static const size_t OUT_BUFFER_LIMIT = 1 * 1024 * 1024;  // 1 MB

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

    // LFU 缓存：缓存房间最近 N 条历史消息
    std::unique_ptr<LFUCache<std::string, std::vector<std::string>>> historyCache_;

    // 专用异步线程池：Redis IO 全部在这里执行，不碰 EventLoop 线程
    std::unique_ptr<ThreadPool> redis_pool_;
};
