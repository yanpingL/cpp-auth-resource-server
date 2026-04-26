#include "redis_client.h"
#include <iostream>

RedisClient* RedisClient::get_instance() {
    static RedisClient instance;
    return &instance;
}

bool RedisClient::connect(const std::string& host, int port) {
    std::lock_guard<std::mutex> lock(mtx);
    context = redisConnect(host.c_str(), port);
    return context != nullptr && !context->err;
}

bool RedisClient::set(const std::string& key, const std::string& value, int expire) {
    std::lock_guard<std::mutex> lock(mtx);
    if (!context || context->err) {
        return false;
    }

    redisReply* reply = (redisReply*)redisCommand(
        context, "SETEX %s %d %s", key.c_str(), expire, value.c_str());

    bool ok = reply && reply->type == REDIS_REPLY_STATUS;
    if (reply) {
        freeReplyObject(reply);
    }
    return ok;
}

std::string RedisClient::get(const std::string& key) {
    std::lock_guard<std::mutex> lock(mtx);
    if (!context || context->err) {
        return "";
    }

    redisReply* reply = (redisReply*)redisCommand(
        context, "GET %s", key.c_str());

    std::string res = (reply && reply->type == REDIS_REPLY_STRING)
                      ? reply->str : "";

    if (reply) {
        freeReplyObject(reply);
    }
    return res;
}

bool RedisClient::del(const std::string& key) {
    std::lock_guard<std::mutex> lock(mtx);
    if (!context || context->err) {
        return false;
    }

    redisReply* reply = (redisReply*)redisCommand(
        context, "DEL %s", key.c_str());

    bool ok = reply && reply->integer > 0;
    if (reply) {
        freeReplyObject(reply);
    }
    return ok;
}
