#include "user_dao.h"
#include "connection_pool.h"
#include <mysql/mysql.h>
#include <iostream>

std::string UserDAO::msg;

std::optional<User> UserDAO::get_user_by_id(int id){
    // get the shared single instance 
    connection_pool* pool = connection_pool::get_instance();
    MYSQL* conn = pool->get_connection();

    if(!conn) return std::nullopt;

    std::string sql = 
        "SELECT id, name, email FROM users WHERE id=" + std::to_string(id);
    
    if(mysql_query(conn, sql.c_str())){
        msg = std::string("DB connection failed.");
        pool->release_connection(conn);
        return std::nullopt;
    }

    MYSQL_RES* result = mysql_store_result(conn);
    if (!result){
        msg = std::string("Query failed.");
        pool->release_connection(conn);
        return std::nullopt;
    }

    MYSQL_ROW row = mysql_fetch_row(result);
    if (!row){
        msg = std::string("result not found.");
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

    if (mysql_query(conn, sql.c_str())) {
        msg = std::string("Query failed.");
        pool->release_connection(conn);
        return false;
    }

    bool success = mysql_affected_rows(conn) > 0;
    if (!success) msg = std::string("Resource not found.");

    pool->release_connection(conn);
    return success;
}



bool UserDAO::create_user(const std::string& sql) {
    connection_pool* pool = connection_pool::get_instance();
    MYSQL* conn = pool->get_connection();

    if (!conn) {
        msg = std::string("DB connection failed.");
        return false;
    }
    bool success = (mysql_query(conn, sql.c_str()) == 0);
    if (!success) msg = std::string("Query failed.");


    pool->release_connection(conn);
    return success;
}



bool UserDAO::update_user(const std::string& sql) {
    connection_pool* pool = connection_pool::get_instance();
    MYSQL* conn = pool->get_connection();

    if (!conn) {
        msg = std::string("DB connection failed.");
        return false;
    }

    if (mysql_query(conn, sql.c_str())) {
        msg = std::string("Query failed.");
        pool->release_connection(conn);
        return false;
    }

    bool success = mysql_affected_rows(conn) > 0;
    if (!success) msg = std::string("Resource not found.");

    pool->release_connection(conn);
    return success;
}




