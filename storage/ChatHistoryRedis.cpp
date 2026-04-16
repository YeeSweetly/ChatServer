// ChatHistoryRedis.cpp
#include "ChatHistoryRedis.h"
#include "../base/Logging.h"
#include <ctime>
#include <sstream>

ChatHistoryRedis& ChatHistoryRedis::getInstance() {
    static ChatHistoryRedis instance;
    return instance;
}

bool ChatHistoryRedis::init(const std::string& host, int port) {
    context_ = redisConnect(host.c_str(), port);
    if (context_ == nullptr || context_->err) {
        LOG << "Redis connection error: "
            << (context_ ? context_->errstr : "can't allocate redis context");
        return false;
    }
    LOG << "Connected to Redis at " << host << ":" << port;
    return true;
}

void ChatHistoryRedis::saveMessage(const std::string& roomId,
                                   const std::string& sender,
                                   const std::string& message) {
    if (!context_) {
        LOG << "Redis not initialized";
        return;
    }
    long long messageId = time(nullptr);
    std::string hashKey = "chat:room:" + roomId + ":msg:" + std::to_string(messageId);
    redisReply* reply = (redisReply*)redisCommand(context_,
        "HMSET %s sender %s content %s timestamp %lld",
        hashKey.c_str(), sender.c_str(), message.c_str(), messageId);
    if (reply) freeReplyObject(reply);

    std::string listKey = "chat:room:" + roomId + ":timeline";
    reply = (redisReply*)redisCommand(context_, "RPUSH %s %lld", listKey.c_str(), messageId);
    if (reply) freeReplyObject(reply);

    // 可选：限制时间线长度（保留最近1000条）
    reply = (redisReply*)redisCommand(context_, "LTRIM %s -1000 -1", listKey.c_str());
    if (reply) freeReplyObject(reply);
}

std::vector<std::string> ChatHistoryRedis::getRecentMessages(const std::string& roomId, int count) {
    std::vector<std::string> result;
    if (!context_ || count <= 0) return result;

    std::string listKey = "chat:room:" + roomId + ":timeline";
    // 获取最后 count 个消息 ID
    redisReply* reply = (redisReply*)redisCommand(context_, "LRANGE %s -%d -1", listKey.c_str(), count);
    if (!reply || reply->type != REDIS_REPLY_ARRAY) {
        if (reply) freeReplyObject(reply);
        return result;
    }

    for (size_t i = 0; i < reply->elements; ++i) {
        if (reply->element[i]->type == REDIS_REPLY_STRING) {
            std::string msgId = reply->element[i]->str;
            std::string hashKey = "chat:room:" + roomId + ":msg:" + msgId;
            redisReply* msgReply = (redisReply*)redisCommand(context_, "HMGET %s sender content", hashKey.c_str());
            if (msgReply && msgReply->type == REDIS_REPLY_ARRAY && msgReply->elements == 2) {
                std::string sender = msgReply->element[0]->str ? msgReply->element[0]->str : "";
                std::string content = msgReply->element[1]->str ? msgReply->element[1]->str : "";
                if (!sender.empty() && !content.empty()) {
                    result.push_back("[" + sender + "] " + content);
                }
            }
            if (msgReply) freeReplyObject(msgReply);
        }
    }
    freeReplyObject(reply);
    return result;
}

ChatHistoryRedis::~ChatHistoryRedis() {
    if (context_) {
        redisFree(context_);
    }
}