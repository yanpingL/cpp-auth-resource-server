#ifndef CONNECTION_POOL_H
#define CONNECTION_POOL_H

#include <mysql/mysql.h>
#include <queue>
#include <string>
#include "thread/locker.h"
#include "utils/logger.h"

class connection_pool {
public:
    static connection_pool* get_instance();

    void init(std::string url, std::string user, std::string password,
              std::string db_name, int port, int max_conn);

    MYSQL* get_connection();
    bool release_connection(MYSQL* conn);
    int get_free_conn();

private:
    connection_pool();
    ~connection_pool();

private:
    int max_conn;
    int cur_conn;
    int free_conn;

    locker lock;
    sem reserve;

    std::queue<MYSQL*> conn_list;

    std::string url;
    std::string user;
    std::string password;
    std::string db_name;
    int port;
};

#endif
