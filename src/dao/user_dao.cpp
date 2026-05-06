#include "user_dao.h"
#include "db/connection_pool.h"
#include "utils/logger.h"
#include "cache/redis_client.h"

#include <mysql/mysql.h>
#include <iostream>

std::string UserDAO::msg;


bool UserDAO::create_user(const std::string& sql) {
    connection_pool* pool = connection_pool::get_instance();
    MYSQL* conn = pool->get_connection();

    if (!conn) {
        msg = std::string("DB connection failed.");
        Logger::get_instance()->log(ERROR, msg);
        return false;
    }
    bool success = (mysql_query(conn, sql.c_str()) == 0);
    if (!success) {
        msg = std::string("Query failed: ") + mysql_error(conn);
        Logger::get_instance()->log(ERROR, msg + " SQL: " + sql);
    }

    pool->release_connection(conn);
    return success;
}



std::optional<User> UserDAO::get_user_by_id(int id){
    // get the shared single instance 
    connection_pool* pool = connection_pool::get_instance();
    MYSQL* conn = pool->get_connection();

    if(!conn) return std::nullopt;

    std::string sql = 
        "SELECT id, name, email FROM users WHERE id=" + std::to_string(id);
    Logger::get_instance()->log(DEBUG, "SQL: " + sql);
    
    if(mysql_query(conn, sql.c_str())){
        msg = std::string("Query failed.");
        Logger::get_instance()->log(ERROR, msg);
        pool->release_connection(conn);
        return std::nullopt;
    }

    MYSQL_RES* result = mysql_store_result(conn);
    if (!result){
        msg = std::string("Query failed.");
        Logger::get_instance()->log(ERROR, msg);
        pool->release_connection(conn);
        return std::nullopt;
    }

    MYSQL_ROW row = mysql_fetch_row(result);
    if (!row){
        msg = std::string("result not found.");
        Logger::get_instance()->log(ERROR, msg);
        mysql_free_result(result);
        pool->release_connection(conn);
        return std::nullopt;
    }

    User user;
    user.id = atoi(row[0]);
    user.name = row[1];
    user.email = row[2];

    mysql_free_result(result);
    pool->release_connection(conn);

    return user;
} 



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
    user.password = row[3];   // ⚠️ add this field

    mysql_free_result(result);
    pool->release_connection(conn);

    return user;
}



bool UserDAO::update_user(const std::string& sql) {
    connection_pool* pool = connection_pool::get_instance();
    MYSQL* conn = pool->get_connection();

    if (!conn) {
        msg = std::string("DB connection failed.");
        Logger::get_instance()->log(ERROR, "DB connection failed.");
        return false;
    }

    if (mysql_query(conn, sql.c_str())) {
        msg = std::string("Query failed.");
        Logger::get_instance()->log(ERROR, "Query failed: " + sql);
        pool->release_connection(conn);
        return false;
    }

    bool success = mysql_affected_rows(conn) > 0;
    if (!success) {
        msg = std::string("Resource not found.");
        Logger::get_instance()->log(ERROR, msg);
    }    
    pool->release_connection(conn);
    return success;
}



bool UserDAO::delete_user(int id) {
    connection_pool* pool = connection_pool::get_instance();
    MYSQL* conn = pool->get_connection();

    if (!conn) {
        msg = std::string("DB connection failed.");
        return false;
    }
    // Need to change the name of the DB to make it changeable 
    std::string sql =
        "DELETE FROM users WHERE id=" + std::to_string(id);
    Logger::get_instance()->log(DEBUG, "SQL: " + sql);

    if (mysql_query(conn, sql.c_str())) {
        msg = std::string("Query failed.");
        Logger::get_instance()->log(ERROR, "Query failed: " + sql);
        pool->release_connection(conn);
        return false;
    }

    bool success = mysql_affected_rows(conn) > 0;
    if (!success) {
        msg = std::string("Resource not found.");
        Logger::get_instance()->log(ERROR, msg);

    }

    pool->release_connection(conn);
    return success;
}


bool UserDAO::validate_token(const std::string& token) {
    // Try Redis first
    std::string user_id =
        RedisClient::get_instance()->get(token);
    
    if (!user_id.empty()){
        return true;  // fast path
    }

    // Redis miss -> fallback to DB
    connection_pool* pool = connection_pool::get_instance();
    MYSQL* conn = pool->get_connection();

    if (!conn) {
        msg = std::string("DB connection failed.");
        Logger::get_instance()->log(ERROR, msg);
        return false;
    }

    std::string sql = "SELECT user_id FROM sessions WHERE token='" + token 
                        + "' AND expires_at > NOW()";

    Logger::get_instance()->log(DEBUG, "SQL: " + sql);

    if (mysql_query(conn, sql.c_str())) {
        msg = "Query failed";
        Logger::get_instance()->log(ERROR, msg);
        pool->release_connection(conn);
        return false;
    }

    MYSQL_RES* result = mysql_store_result(conn);
    if (!result) {
        msg = "User not found";
        Logger::get_instance()->log(ERROR, msg);
        pool->release_connection(conn);
        return false;
    }

    MYSQL_ROW row = mysql_fetch_row(result);

    // Cache it back to Redis
    if (row != nullptr){
        std::string uid = row[0];
        RedisClient::get_instance()->set(token, uid, 3600);
    }

    mysql_free_result(result);
    pool->release_connection(conn);

    return row != nullptr;
}



bool UserDAO::delete_session(const std::string& token) {
    // delete from Redis
    RedisClient::get_instance()->del(token);

    // delete from DB
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

    pool->release_connection(conn);
    return success;
}



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

    // cache back to Redis
    RedisClient::get_instance()->set(token, std::to_string(user_id), 3600);

    mysql_free_result(result);
    pool->release_connection(conn);

    return user_id;
}
