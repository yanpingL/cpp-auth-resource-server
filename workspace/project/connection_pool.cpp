#include <iostream>
#include "connection_pool.h"
#include "locker.h"

connection_pool::connection_pool(){
    cur_conn = 0;
    free_conn = 0;
}

connection_pool::~connection_pool() {
    while (!conn_list.empty()) {
        MYSQL* conn = conn_list.front();
        conn_list.pop();
        mysql_close(conn);
    }
}

connection_pool* connection_pool::get_instance() {
    static connection_pool instance;
    return &instance;
}

void connection_pool::init(std::string url, std::string user,
    std::string password, std::string db_name,
    int port, int max_conn) {

    this->url = url;
    this->user = user;
    this->password = password;
    this->db_name = db_name;
    this->port = port;
    this->max_conn = max_conn;

    for (int i = 0; i < max_conn; i++) {    
        MYSQL* conn = mysql_init(NULL);

        if (!conn) {
            std::cout << "MySQL init error\n";
            Logger::get_instance()->log(ERROR, "MySQL connect error");
            exit(1);
        }

        conn = mysql_real_connect(conn,
           url.c_str(),
           user.c_str(),
           password.c_str(),
           db_name.c_str(),
           port,
           NULL,
           0);

        if (!conn) {
            std::cout << "MySQL connect error\n";
            Logger::get_instance()->log(ERROR, "MySQL connect error");
            exit(1);
        }

        conn_list.push(conn);
        free_conn++;
    }
    reserve = sem(free_conn);
}


MYSQL* connection_pool::get_connection() {
    MYSQL* conn = NULL;

    reserve.wait();     // wait if no available connection
    lock.lock();
    if (conn_list.empty()) {
        lock.unlock();
        return NULL;
    }

    conn = conn_list.front();
    conn_list.pop();

    free_conn--;
    cur_conn++;
    lock.unlock();

    return conn;
}


bool connection_pool::release_connection(MYSQL* conn) {
    if (!conn) return false;

    lock.lock();
    conn_list.push(conn);
    free_conn++;
    cur_conn--;

    lock.unlock();
    reserve.post();

    return true;
}


int connection_pool::get_free_conn() {
    return free_conn;
}