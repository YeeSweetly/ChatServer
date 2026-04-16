// ChatHistoryRedis.h
#pragma once

#include <string>
#include <vector>
#include <hiredis/hiredis.h>
#include "../base/noncopyable.h"

class ChatHistoryRedis : noncopyable {
public:
    static ChatHistoryRedis& getInstance();

    bool init(const std::string& host = "127.0.0.1", int port = 6379);

    void saveMessage(const std::string& roomId,
                     const std::string& sender,
                     const std::string& message);

    // 新增：获取最近 N 条消息
    std::vector<std::string> getRecentMessages(const std::string& roomId, int count);

    ~ChatHistoryRedis();

private:
    ChatHistoryRedis() = default;
    redisContext* context_ = nullptr;
};