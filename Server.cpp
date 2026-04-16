// Server.cpp
#include "Server.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <functional>
#include "Util.h"
#include "base/Logging.h"
#include "storage/ChatHistoryRedis.h"   // 新增

Server* g_server = nullptr;

Server::Server(EventLoop *loop, int threadNum, int port)
    : loop_(loop),
      threadNum_(threadNum),
      eventLoopThreadPool_(new EventLoopThreadPool(loop_, threadNum)),
      started_(false),
      acceptChannel_(new Channel(loop_)),
      port_(port),
      listenFd_(socket_bind_listen(port_)) {
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

    // 初始化 LFU 缓存，容量 1000
    historyCache_.reset(new LFUCache<std::string, std::vector<std::string>>(1000));
}

void Server::start() {
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

void Server::broadcastMessage(const std::string& msg, SP_ChatSession exclude) {
    MutexLockGuard lock(mutex_);
    for (auto& pair : sessions_) {
        auto& sess = pair.second;
        if (sess != exclude) {
            sess->sendMessage(msg);
        }
    }
    // 当有新消息时，清除相关历史缓存（使缓存失效，下次查询时会重新加载）
    // 这里简单清除所有以 "global_room:" 开头的缓存键
    // 实际可按需实现更细粒度的失效策略
    historyCache_->erase("global_room:5");
    historyCache_->erase("global_room:10");
    historyCache_->erase("global_room:20");
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