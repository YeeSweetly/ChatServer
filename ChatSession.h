// ChatSession.h  —  修复版
// 修复点：
//   1. sendMessage 增加 outBuffer_ 大小检查，超限时关闭连接（防止内存无限增长）
#pragma once

#include <sys/epoll.h>
#include <unistd.h>
#include <functional>
#include <memory>
#include <string>
#include <map>
#include "Timer.h"
#include <atomic>

class EventLoop;
class TimerNode;
class Channel;

enum SessionState {
    STATE_EXPECT_USERNAME = 1,
    STATE_READY,
    STATE_DISCONNECTING,
    STATE_DISCONNECTED
};

class ChatSession : public std::enable_shared_from_this<ChatSession> {
public:
    ChatSession(EventLoop *loop, int connfd);
    ~ChatSession();

    void reset();
    void seperateTimer();
    void linkTimer(std::shared_ptr<TimerNode> mtimer) { timer_ = mtimer; }
    std::shared_ptr<Channel> getChannel() { return channel_; }
    EventLoop *getLoop() { return loop_; }
    void handleClose();
    void newEvent();
    void setUsername(const std::string& name) { username_ = name; }
    std::string getUsername() const { return username_; }
    int getFd() const { return fd_; }

    // 发送消息（线程安全）
    // 修复：发送前检查 outBuffer_ 大小，超过 OUT_BUFFER_LIMIT 时强制断开
    void sendMessage(const std::string& msg);

private:
    void handleRead();
    void handleWrite();
    void handleConn();
    void handleError(int fd, int err_num, const std::string& short_msg);
    void processInput(const std::string& line);
    void sendHistoryMessages();

    EventLoop *loop_;
    std::shared_ptr<Channel> channel_;
    int fd_;
    std::string inBuffer_;
    std::string outBuffer_;
    bool error_;
    SessionState state_;
    std::string username_;
    std::weak_ptr<TimerNode> timer_;
    std::atomic<bool> closed_{false};
    size_t inBufPos_ = 0;   // 读指针
};

typedef std::shared_ptr<ChatSession> SP_ChatSession;
