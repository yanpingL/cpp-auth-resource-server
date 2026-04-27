#ifndef REDIS_CLIENT_H
#define REDIS_CLIENT_H

#include <hiredis/hiredis.h>
#include <string>
#include <mutex>

class RedisClient {
public:
    static RedisClient* get_instance();

    bool connect(const std::string& host, int port);

    bool set(const std::string& key, const std::string& value, int expire);
    std::string get(const std::string& key);
    
    bool del(const std::string& key);

private:
    RedisClient() : context(nullptr) {}
    redisContext* context;
    std::mutex mtx;
};

#endif
