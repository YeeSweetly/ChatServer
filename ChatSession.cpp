// @Author Lin Ya (refactored for chat room)
#include "ChatSession.h"
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <iostream>
#include <cstdlib>
#include "Channel.h"
#include "EventLoop.h"
#include "Server.h"
#include "Util.h"
#include "base/Logging.h"
#include "storage/ChatHistoryRedis.h"

using namespace std;

const __uint32_t DEFAULT_EVENT = EPOLLIN | EPOLLET | EPOLLONESHOT;
const int DEFAULT_EXPIRED_TIME = 2000;              // ms
const int DEFAULT_KEEP_ALIVE_TIME = 5 * 60 * 1000;  // ms

ChatSession::ChatSession(EventLoop *loop, int connfd)
    : loop_(loop),
      channel_(new Channel(loop, connfd)),
      fd_(connfd),
      error_(false),
      state_(STATE_EXPECT_USERNAME) {
    channel_->setReadHandler(bind(&ChatSession::handleRead, this));
    channel_->setWriteHandler(bind(&ChatSession::handleWrite, this));
    channel_->setConnHandler(bind(&ChatSession::handleConn, this));
}

ChatSession::~ChatSession() {
    close(fd_);
}

void ChatSession::reset() {
    inBuffer_.clear();
    outBuffer_.clear();
    error_ = false;
    state_ = STATE_EXPECT_USERNAME;
    username_.clear();
    if (timer_.lock()) {
        shared_ptr<TimerNode> my_timer(timer_.lock());
        my_timer->clearReq();
        timer_.reset();
    }
}

void ChatSession::seperateTimer() {
    if (timer_.lock()) {
        shared_ptr<TimerNode> my_timer(timer_.lock());
        my_timer->clearReq();
        timer_.reset();
    }
}

void ChatSession::handleRead() {
    __uint32_t &events_ = channel_->getEvents();
    do {
        bool zero = false;
        int read_num = readn(fd_, inBuffer_, zero);
        LOG << "Received " << read_num << " bytes from fd " << fd_;
        if (state_ == STATE_DISCONNECTING) {
            inBuffer_.clear();
            break;
        }
        if (read_num < 0) {
            error_ = true;
            handleError(fd_, 400, "Read error");
            break;
        } else if (zero) {
            // 对端关闭
            state_ = STATE_DISCONNECTING;
            break;
        }

        // 按行处理输入
        size_t pos;
        while ((pos = inBuffer_.find('\n')) != string::npos) {
            string line = inBuffer_.substr(0, pos);
            if (!line.empty() && line.back() == '\r')
                line.pop_back();   // 去除 '\r'
            inBuffer_.erase(0, pos + 1);
            processInput(line);
            if (state_ == STATE_DISCONNECTING || state_ == STATE_DISCONNECTED)
                break;
        }
    } while (false);

    if (!error_) {
        if (outBuffer_.size() > 0) {
            handleWrite();
        }
        if (!error_ && state_ != STATE_DISCONNECTED) {
            events_ |= EPOLLIN;
        }
    }
}

void ChatSession::processInput(const string& line) {
    if (line.empty()) return;

    if (state_ == STATE_EXPECT_USERNAME) {
        // 第一条消息作为用户名
        if (line.length() > 32) {
            sendMessage("ERROR Username too long (max 32)\n");
            handleClose();
            return;
        }
        username_ = line;
        state_ = STATE_READY;
        LOG << "User " << username_ << " logged in, fd=" << fd_;
        // 发送欢迎消息
        sendMessage("OK Welcome to chat room, " + username_ + "!\n");

        // 发送历史消息
        sendHistoryMessages();
        // 通知其他用户有人加入
        extern Server* g_server;  // 声明外部全局指针，在 Main.cpp 中定义
        if (g_server) {
            string joinMsg = "*** " + username_ + " joined the chat.\n";
            g_server->broadcastMessage(joinMsg, shared_from_this());
        }
        return;
    }

    if (state_ == STATE_READY) {
        if (line == "QUIT") {
            sendMessage("BYE\n");
            handleClose();
            return;
        }

        // 普通聊天消息
        std::string msg = "[" + username_ + "] " + line + "\n";
        LOG << "Broadcast: " << msg;

        ChatHistoryRedis::getInstance().saveMessage("global_room", username_, line);

        extern Server* g_server;
        if (g_server) {
            g_server->broadcastMessage(msg, shared_from_this());
        }
    }
}

void ChatSession::handleWrite() {
    if (!error_ && state_ != STATE_DISCONNECTED) {
        __uint32_t &events_ = channel_->getEvents();
        if (writen(fd_, outBuffer_) < 0) {
            error_ = true;
            events_ = 0;
        }
        if (outBuffer_.size() > 0)
            events_ |= EPOLLOUT;
    }
}

void ChatSession::handleConn() {
    seperateTimer();
    __uint32_t &events_ = channel_->getEvents();
    if (!error_ && state_ == STATE_READY) {
        if (events_ != 0) {
            int timeout = DEFAULT_EXPIRED_TIME;
            events_ |= EPOLLET;
            loop_->updatePoller(channel_, timeout);
        } else {
            events_ |= (EPOLLIN | EPOLLET);
            int timeout = DEFAULT_KEEP_ALIVE_TIME;
            loop_->updatePoller(channel_, timeout);
        }
    } else if (!error_ && state_ == STATE_DISCONNECTING && (events_ & EPOLLOUT)) {
        events_ = (EPOLLOUT | EPOLLET);
    } else {
        loop_->runInLoop(bind(&ChatSession::handleClose, shared_from_this()));
    }
}

void ChatSession::sendMessage(const string& msg) {
    loop_->runInLoop([this, msg]() {
        if (state_ != STATE_DISCONNECTED) {
            outBuffer_ += msg;
            __uint32_t &events_ = channel_->getEvents();
            events_ |= EPOLLOUT;
            loop_->updatePoller(channel_, DEFAULT_EXPIRED_TIME);
        }
    });
}

void ChatSession::handleError(int fd, int err_num, const string& short_msg) {
    // 简单处理，直接关闭
    handleClose();
}

void ChatSession::handleClose() {
    state_ = STATE_DISCONNECTED;
    LOG << "Connection closed, fd=" << fd_ << ", username=" << username_;
    // 通知 Server 移除自己
    extern Server* g_server;
    if (g_server) {
        g_server->removeSession(shared_from_this());
        if (!username_.empty()) {
            string leaveMsg = "*** " + username_ + " left the chat.\n";
            g_server->broadcastMessage(leaveMsg, shared_from_this());
        }
    }
    loop_->removeFromPoller(channel_);
}

void ChatSession::newEvent() {
    channel_->setEvents(DEFAULT_EVENT);
    loop_->addToPoller(channel_, DEFAULT_EXPIRED_TIME);
}

void ChatSession::sendHistoryMessages() {
    extern Server* g_server;
    if (!g_server) return;

    // 获取最近 10 条历史记录（可配置）
    const int historyCount = 10;
    auto history = g_server->getRecentHistory("global_room", historyCount);

    if (history.empty()) {
        sendMessage("--- No recent messages ---\n");
    } else {
        sendMessage("--- Recent " + std::to_string(history.size()) + " messages ---\n");
        for (const auto& msg : history) {
            sendMessage(msg + "\n");
        }
        sendMessage("--- End of history ---\n");
    }
}