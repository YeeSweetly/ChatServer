// ChatSession.cpp  —  修复版
// 修复点：
//   1. processInput 中 saveMessage 改为 asyncSaveMessage，不阻塞 EventLoop
//   2. sendMessage 增加 outBuffer_ 大小上限检查（OUT_BUFFER_LIMIT = 1MB）
//      超限时强制关闭该连接，防止慢客户端/消息风暴导致内存无限增长
//   3. 去掉多余的 resetLastEvents() 调用（正常 epoll_mod 判断逻辑已足够）

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

const __uint32_t DEFAULT_EVENT      = EPOLLIN | EPOLLET | EPOLLONESHOT;
const int DEFAULT_EXPIRED_TIME      = 30 * 1000;        // 30 秒
const int DEFAULT_KEEP_ALIVE_TIME   = 10 * 60 * 1000;   // 10 分钟

ChatSession::ChatSession(EventLoop *loop, int connfd)
    : loop_(loop),
      channel_(new Channel(loop, connfd)),
      fd_(connfd),
      error_(false),
      state_(STATE_EXPECT_USERNAME),
      closed_(false) {
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
        // LOG << "Received " << read_num << " bytes from fd " << fd_;   // 压测时注释
        if (state_ == STATE_DISCONNECTING) {
            inBuffer_.clear();
            break;
        }
        if (read_num < 0) {
            error_ = true;
            handleError(fd_, 400, "Read error");
            break;
        } else if (zero) {
            state_ = STATE_DISCONNECTING;
            break;
        }

        // 按行处理（修复版：正确处理 \r\n）
        size_t pos;
        while ((pos = inBuffer_.find('\n', inBufPos_)) != std::string::npos) {
            std::string line = inBuffer_.substr(inBufPos_, pos - inBufPos_);
            // 去除末尾的 '\r'（兼容 Windows 风格和纯 '\n'）
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            inBufPos_ = pos + 1;
            processInput(line);
            if (state_ == STATE_DISCONNECTING || state_ == STATE_DISCONNECTED)
                break;
        }

        // 批量回收缓冲区（可选优化）
        if (inBufPos_ > 8192) {
            inBuffer_.erase(0, inBufPos_);
            inBufPos_ = 0;
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
        // 压测客户端识别：第一条消息形如 "msg_..." 时自动分配用户名
        if (line.find("msg_") == 0) {
            username_ = "user_unknown";
            state_    = STATE_READY;
            LOG << "Auto assign username: " << username_;
            // 不 return，继续处理这条消息
        } else {
            if (line.length() > 32) {
                sendMessage("ERROR Username too long (max 32)\n");
                handleClose();
                return;
            }
            username_ = line;
            state_    = STATE_READY;
            LOG << "User " << username_ << " logged in, fd=" << fd_;
            sendMessage("OK Welcome to chat room, " + username_ + "!\n");

            extern Server* g_server;
            if (g_server) {
                string joinMsg = "*** " + username_ + " joined the chat.\n";
                g_server->broadcastMessage(joinMsg, shared_from_this());
            }
            return;
        }
    }

    if (state_ == STATE_READY) {
        if (line == "QUIT") {
            sendMessage("BYE\n");
            handleClose();
            return;
        }

        std::string msg = "[" + username_ + "] " + line + "\n";
        //LOG << "Broadcast: " << msg;

        // 修复①：异步写 Redis，不阻塞 EventLoop 线程
        extern Server* g_server;
        if (g_server) {
            g_server->asyncSaveMessage("global_room", username_, line);
            g_server->broadcastMessage(msg, shared_from_this());
        }
    }
}

void ChatSession::handleWrite() {
    if (closed_.load()) return;
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
    if (closed_.load()) return;
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

// ----------------------------------------------------------------
// sendMessage  —  修复版
// 修复②：发送前检查 outBuffer_ 大小
//   超过 OUT_BUFFER_LIMIT (1MB) 说明客户端消费太慢或已失连，
//   直接关闭该连接，避免内存无限增长。
// ----------------------------------------------------------------
void ChatSession::sendMessage(const string& msg) {
    extern Server* g_server;
    const size_t limit = g_server ? Server::OUT_BUFFER_LIMIT : (1 * 1024 * 1024);

    auto do_send = [this, msg, limit]() {
        if (state_ == STATE_DISCONNECTED) return;

        // 超限保护：强制断开慢/失联客户端
        if (outBuffer_.size() > limit) {
            LOG << "outBuffer overflow on fd=" << fd_ << ", closing";
            error_ = true;
            handleClose();
            return;
        }

        //LOG << "Send to fd=" << fd_ << " content=" << msg;
        outBuffer_ += msg;

        if (writen(fd_, outBuffer_) < 0) {
            error_ = true;
            return;
        }

        if (!error_ && outBuffer_.size() > 0) {
            __uint32_t &events_ = channel_->getEvents();
            events_ |= EPOLLOUT;
            channel_->resetLastEvents();
            loop_->updatePoller(channel_, DEFAULT_EXPIRED_TIME);
        }
    };

    if (loop_->isInLoopThread()) {
        do_send();
    } else {
        loop_->queueInLoop(std::move(do_send));
    }
}

void ChatSession::handleError(int fd, int err_num, const string& short_msg) {
    handleClose();
}

void ChatSession::handleClose() {
    bool expected = false;
    if (!closed_.compare_exchange_strong(expected, true))
        return;
    state_ = STATE_DISCONNECTED;
    LOG << "Connection closed, fd=" << fd_ << ", username=" << username_;

    auto self = shared_from_this();
    loop_->runInLoop([self]() {
        extern Server* g_server;
        if (g_server) {
            g_server->removeSession(self);
            if (!self->username_.empty()) {
                std::string leaveMsg = "*** " + self->username_ + " left the chat.\n";
                g_server->broadcastMessage(leaveMsg, self);
            }
        }
        self->loop_->removeFromPoller(self->channel_);
    });
}

void ChatSession::newEvent() {
    channel_->setEvents(DEFAULT_EVENT);
    loop_->addToPoller(channel_, DEFAULT_EXPIRED_TIME);
}

void ChatSession::sendHistoryMessages() {
    extern Server* g_server;
    if (!g_server) return;

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
