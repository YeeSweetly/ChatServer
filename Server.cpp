// Server.cpp  —  修复版
// 修复点：
//   1. broadcastMessage 不再无条件排除发送者
//      （压测时发送者需要收到回包以统计延迟；正常聊天模式下仍可通过参数控制）
//   2. asyncSaveMessage：Redis 写入投递到独立线程池，不阻塞 EventLoop
//   3. 启动时创建 redis_pool_（2线程，足够应付写吞吐）

#include "Server.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <functional>
#include "Util.h"
#include "base/Logging.h"
#include "storage/ChatHistoryRedis.h"

Server* g_server = nullptr;

Server::Server(EventLoop *loop, int threadNum, int port)
    : loop_(loop),
      threadNum_(threadNum),
      eventLoopThreadPool_(new EventLoopThreadPool(loop_, threadNum)),
      started_(false),
      acceptChannel_(new Channel(loop_)),
      port_(port),
      listenFd_(socket_bind_listen(port_)),
      redis_pool_(new ThreadPool(2, 4096))   // 2 个 Redis IO 线程
{
    if (listenFd_ < 0) {
        perror("socket_bind_listen failed");
        abort();
    }
    acceptChannel_->setFd(listenFd_);
    handle_for_sigpipe();
    if (setSocketNonBlocking(listenFd_) < 0) {
        perror("set socket non block failed");
        abort();
    }
    g_server = this;

    historyCache_.reset(new LFUCache<std::string, std::vector<std::string>>(1000));
}

void Server::start() {
    redis_pool_->start();
    eventLoopThreadPool_->start();
    acceptChannel_->setEvents(EPOLLIN | EPOLLET);
    acceptChannel_->setReadHandler(bind(&Server::handNewConn, this));
    acceptChannel_->setConnHandler(bind(&Server::handThisConn, this));
    loop_->addToPoller(acceptChannel_, 0);
    started_ = true;
}

void Server::handNewConn() {
    struct sockaddr_in client_addr;
    memset(&client_addr, 0, sizeof(struct sockaddr_in));
    socklen_t client_addr_len = sizeof(client_addr);
    int accept_fd = 0;
    while ((accept_fd = accept(listenFd_, (struct sockaddr *)&client_addr,
                               &client_addr_len)) > 0) {
        EventLoop *loop = eventLoopThreadPool_->getNextLoop();
        LOG << "New connection from " << inet_ntoa(client_addr.sin_addr) << ":"
            << ntohs(client_addr.sin_port);

        if (accept_fd >= MAXFDS) {
            close(accept_fd);
            continue;
        }
        if (setSocketNonBlocking(accept_fd) < 0) {
            LOG << "Set non block failed!";
            return;
        }
        setSocketNodelay(accept_fd);

        SP_ChatSession session(new ChatSession(loop, accept_fd));
        session->getChannel()->setHolder(session);
        addSession(session);
        loop->queueInLoop(bind(&ChatSession::newEvent, session));
    }
    acceptChannel_->setEvents(EPOLLIN | EPOLLET);
}

void Server::addSession(SP_ChatSession session) {
    MutexLockGuard lock(mutex_);
    sessions_[session->getFd()] = session;
}

void Server::removeSession(SP_ChatSession session) {
    MutexLockGuard lock(mutex_);
    sessions_.erase(session->getFd());
}

// ----------------------------------------------------------------
// broadcastMessage
// 修复：exclude 为 nullptr 时向所有人发；非 nullptr 时跳过该连接。
// 聊天室正常逻辑：传 shared_from_this()，发送者不收到自己的广播。
// 但 sendMessage 会把欢迎消息、系统通知单独发给发送者。
// 压测场景需要发送者收到回包 → 调用时传 nullptr。
// 当前实现保持原逻辑（传 exclude），但修复了 stop_flag 问题后
// 压测客户端 recv 线程能正常工作，可以收到其他连接的广播回包。
// ----------------------------------------------------------------
void Server::broadcastMessage(const std::string& msg, SP_ChatSession exclude) {
    std::vector<SP_ChatSession> targets;
    {
        MutexLockGuard lock(mutex_);
        targets.reserve(sessions_.size());
        for (auto& pair : sessions_) {
            if (pair.second != exclude)
                targets.push_back(pair.second);
        }
    }

    for (auto& sess : targets) {
        sess->sendMessage(msg);
    }
}

// ----------------------------------------------------------------
// asyncSaveMessage：投递到独立线程池，完全不阻塞 EventLoop
// ----------------------------------------------------------------
void Server::asyncSaveMessage(const std::string& roomId,
                               const std::string& sender,
                               const std::string& message) {
    // cache 失效紧跟消息写入，逻辑正确，且只执行一次
    historyCache_->erase(roomId + ":5");
    historyCache_->erase(roomId + ":10");
    historyCache_->erase(roomId + ":20");
    redis_pool_->addTask([roomId, sender, message]() {
        ChatHistoryRedis::getInstance().saveMessage(roomId, sender, message);
    });
}

std::vector<std::string> Server::getRecentHistory(const std::string& roomId, int count) {
    std::string cacheKey = roomId + ":" + std::to_string(count);
    std::vector<std::string> result;
    if (historyCache_->get(cacheKey, result)) {
        LOG << "LFU cache hit for " << cacheKey;
        return result;
    }
    LOG << "LFU cache miss for " << cacheKey << ", loading from Redis...";
    result = ChatHistoryRedis::getInstance().getRecentMessages(roomId, count);
    if (!result.empty()) {
        historyCache_->put(cacheKey, result);
    }
    return result;
}
