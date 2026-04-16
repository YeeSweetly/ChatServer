// @Author Lin Ya (refactored for chat room)
#pragma once

#include <sys/epoll.h>
#include <unistd.h>
#include <functional>
#include <memory>
#include <string>
#include <map>
#include "Timer.h"

class EventLoop;
class TimerNode;
class Channel;

enum SessionState {
    STATE_EXPECT_USERNAME = 1,   // 等待用户名
    STATE_READY,                 // 已登录，可以聊天
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
    void sendMessage(const std::string& msg);   // 发送消息（线程安全）

private:
    void handleRead();
    void handleWrite();
    void handleConn();
    void handleError(int fd, int err_num, const std::string& short_msg);
    void processInput(const std::string& line); // 处理一行输入
    void sendHistoryMessages();   // 发送历史消息给当前用户

    EventLoop *loop_;
    std::shared_ptr<Channel> channel_;
    int fd_;
    std::string inBuffer_;
    std::string outBuffer_;
    bool error_;
    SessionState state_;
    std::string username_;
    std::weak_ptr<TimerNode> timer_;
};

typedef std::shared_ptr<ChatSession> SP_ChatSession;