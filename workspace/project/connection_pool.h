#ifndef CONNECTION_POOL_H
#define CONNECTION_POOL_H

#include <mysql/mysql.h>
#include <queue>
#include <string>
#include "locker.h"

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
    int max_conn; // maximum # connection to DB
    int cur_conn; // # connection to DB currently being used
    int free_conn; // # connections to DB currently free

    locker lock; 
    sem reserve; 

    // queue of MYSQL* to store the pointer of DB connections
    std::queue<MYSQL*> conn_list; 
    

    std::string url;          // container name 
    std::string user;        // user name
    std::string password;   // password
    std::string db_name;   // db name
    int port;
};

#endif