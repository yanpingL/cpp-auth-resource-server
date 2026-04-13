#include "redis_client.h"
#include <iostream>

RedisClient* RedisClient::get_instance() {
    static RedisClient instance;
    return &instance;
}

bool RedisClient::connect(const std::string& host, int port) {
    context = redisConnect(host.c_str(), port);
    return context != nullptr && !context->err;
}

bool RedisClient::set(const std::string& key, const std::string& value, int expire) {
    redisReply* reply = (redisReply*)redisCommand(
        context, "SETEX %s %s %d", key.c_str(), value.c_str(), expire);

    bool ok = reply && reply->type == REDIS_REPLY_STATUS;
    freeReplyObject(reply);
    return ok;
}

std::string RedisClient::get(const std::string& key) {
    redisReply* reply = (redisReply*)redisCommand(
        context, "GET %s", key.c_str());

    std::string res = (reply && reply->type == REDIS_REPLY_STRING)
                      ? reply->str : "";

    freeReplyObject(reply);
    return res;
}

bool RedisClient::del(const std::string& key) {
    redisReply* reply = (redisReply*)redisCommand(
        context, "DEL %s", key.c_str());

    bool ok = reply && reply->integer > 0;
    freeReplyObject(reply);
    return ok;
}