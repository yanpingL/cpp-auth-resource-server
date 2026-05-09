#include "user_dao.h"
#include "db/connection_pool.h"
#include "utils/logger.h"
#include "cache/redis_client.h"

#include <mysql/mysql.h>
#include <iostream>

std::string UserDAO::msg;


// Executes an INSERT-style user/session SQL statement.
bool UserDAO::create_user(const User& user) {
    connection_pool* pool = connection_pool::get_instance();
    MYSQL* conn = pool->get_connection();

    // Check the connection acquisition success
    if (!conn) {
        msg = std::string("DB connection failed.");
        Logger::get_instance()->log(ERROR, msg);
        return false;
    }

    // Generate the query statement
    std::string sql = 
        "INSERT INTO users (name, email, password) VALUES ('" + 
        user.name + "', '" + 
        user.email + "', '" + 
        user.password + "')";


    bool success = (mysql_query(conn, sql.c_str()) == 0);
    if (!success) {
        msg = std::string("Query failed: ") + mysql_error(conn);
        Logger::get_instance()->log(ERROR, msg + " SQL: " + sql);
    }

    pool->release_connection(conn);
    return success;
}

// Loads one user by email for login.
std::optional<User> UserDAO::get_user_by_email(const std::string& email){

    connection_pool* pool = connection_pool::get_instance();
    MYSQL* conn = pool->get_connection();

    if(!conn) {
        msg = std::string("DB connection failed.");
        Logger::get_instance()->log(ERROR, msg);
        return std::nullopt;
    }

    std::string sql =
        "SELECT id, name, email, password FROM users WHERE email='" + email + "'";

    Logger::get_instance()->log(DEBUG, "SQL: " + sql);

    if(mysql_query(conn, sql.c_str())){
        msg = "Query failed";
        Logger::get_instance()->log(ERROR, msg);
        pool->release_connection(conn);
        return std::nullopt;
    }

    MYSQL_RES* result = mysql_store_result(conn);
    if (!result){
        msg = "User not found";
        Logger::get_instance()->log(ERROR, msg);

        pool->release_connection(conn);
        return std::nullopt;
    }

    MYSQL_ROW row = mysql_fetch_row(result);
    if (!row){
        msg = "User not found";
        Logger::get_instance()->log(ERROR, msg);

        mysql_free_result(result);
        pool->release_connection(conn);
        return std::nullopt;
    }

    User user;
    user.id = atoi(row[0]);
    user.name = row[1];
    user.email = row[2];
    user.password = row[3];

    mysql_free_result(result);
    pool->release_connection(conn);

    return user;
}

// Creates a login session in Redis and MySQL.
bool UserDAO::create_session(int user_id, const std::string& token, int ttl_seconds) {
    RedisClient::get_instance()->set(
        token,
        std::to_string(user_id),
        ttl_seconds
    );

    connection_pool* pool = connection_pool::get_instance();
    MYSQL* conn = pool->get_connection();

    if (!conn) {
        msg = std::string("DB connection failed.");
        Logger::get_instance()->log(ERROR, msg);
        RedisClient::get_instance()->del(token);
        return false;
    }

    std::string sql =
        "INSERT INTO sessions (user_id, token, expires_at) VALUES (" +
        std::to_string(user_id) + ", '" + token + "', NOW() + INTERVAL " +
        std::to_string(ttl_seconds) + " SECOND)";

    Logger::get_instance()->log(DEBUG, "SQL: " + sql);

    bool success = (mysql_query(conn, sql.c_str()) == 0);
    if (!success) {
        msg = std::string("Query failed: ") + mysql_error(conn);
        Logger::get_instance()->log(ERROR, msg + " SQL: " + sql);
        RedisClient::get_instance()->del(token);
    }

    pool->release_connection(conn);
    return success;
}


// Removes a session token from Redis and MySQL.
bool UserDAO::delete_session(const std::string& token) {
    RedisClient::get_instance()->del(token);

    connection_pool* pool = connection_pool::get_instance();
    MYSQL* conn = pool->get_connection();

    if (!conn) {
        msg = std::string("DB connection failed.");
        Logger::get_instance()->log(ERROR, msg);
        return false;
    }

    std::string sql =
        "DELETE FROM sessions WHERE token='" + token + "'";

    bool success = (mysql_query(conn, sql.c_str()) == 0);
    if (!success) {
        msg = std::string("Query failed: ") + mysql_error(conn);
        Logger::get_instance()->log(ERROR, msg + " SQL: " + sql);
    }

    pool->release_connection(conn);
    return success;
}



// Resolves a valid session token to the owning user id.
std::optional<int> UserDAO::get_user_id_from_token(const std::string& token) {

    std::string uid = RedisClient::get_instance()->get(token);
    if (!uid.empty()) {
        return std::stoi(uid);
    }

    // data miss in Redis, fallback to DB
    connection_pool* pool = connection_pool::get_instance();
    MYSQL* conn = pool->get_connection();

    std::string sql =
        "SELECT user_id FROM sessions WHERE token='" + token +
        "' AND expires_at > NOW()";

    if (mysql_query(conn, sql.c_str())) {
        msg = "Query failed";
        Logger::get_instance()->log(ERROR, msg);
        pool->release_connection(conn);
        return std::nullopt;
    }

    MYSQL_RES* result = mysql_store_result(conn);
    MYSQL_ROW row = mysql_fetch_row(result);

    if (!row) {
        msg = "User not found";
        Logger::get_instance()->log(ERROR, msg);
        mysql_free_result(result);
        pool->release_connection(conn);
        return std::nullopt;
    }

    int user_id = atoi(row[0]);

    RedisClient::get_instance()->set(token, std::to_string(user_id), 3600);

    mysql_free_result(result);
    pool->release_connection(conn);

    return user_id;
}
