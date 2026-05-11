#include <iostream>
#include "connection_pool.h"
#include "thread/locker.h"

// Initializes connection counters for the singleton pool.
connection_pool::connection_pool(){
    cur_conn = 0;
    free_conn = 0;
}

// Closes all PostgreSQL connections still owned by the pool.
connection_pool::~connection_pool() {
    while (!conn_list.empty()) {
        PGconn* conn = conn_list.front();
        conn_list.pop();
        PQfinish(conn);
    }
}

// Returns the shared PostgreSQL connection pool instance.
connection_pool* connection_pool::get_instance() {
    static connection_pool instance;
    return &instance;
}

// Opens the configured number of PostgreSQL connections.
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
        std::string conninfo =
            "host=" + url +
            " port=" + std::to_string(port) +
            " dbname=" + db_name +
            " user=" + user +
            " password=" + password;

        PGconn* conn = PQconnectdb(conninfo.c_str());

        if (PQstatus(conn) != CONNECTION_OK) {
            std::string error = conn ? PQerrorMessage(conn) : "PQconnectdb returned null";
            std::cout << "PostgreSQL connect error: " << error << "\n";
            Logger::get_instance()->log(ERROR, "PostgreSQL connect error: " + error);
            if (conn) {
                PQfinish(conn);
            }
            exit(1);
        }

        conn_list.push(conn);
        free_conn++;
    }
    if (!reserve.init(free_conn)) {
        Logger::get_instance()->log(ERROR, "Semaphore init error");
        exit(1);
    }
}

// Blocks until a connection is available, then leases it to the caller.
PGconn* connection_pool::get_connection() {
    PGconn* conn = NULL;

    reserve.wait();
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

// Returns a leased connection back to the pool.
bool connection_pool::release_connection(PGconn* conn) {
    if (!conn) return false;

    lock.lock();
    conn_list.push(conn);
    free_conn++;
    cur_conn--;

    lock.unlock();
    reserve.post();

    return true;
}

// Reports the number of currently idle PostgreSQL connections.
int connection_pool::get_free_conn() {
    return free_conn;
}
