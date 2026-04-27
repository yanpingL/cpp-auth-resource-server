#include "resource_dao.h"
#include "db/connection_pool.h"
#include "utils/logger.h"

#include <mysql/mysql.h>
#include <iostream>

std::string ResourceDAO::msg;

bool ResourceDAO::create_resource(const std::string& sql){

    connection_pool* pool = connection_pool::get_instance();
    MYSQL* conn = pool->get_connection();

    if (!conn) {
        msg = std::string("DB connection failed.");
        Logger::get_instance()->log(ERROR, msg);
        return false;
    }

    bool success = (mysql_query(conn, sql.c_str()) == 0);
    if (!success) {
        msg = std::string("Query failed.");
        Logger::get_instance()->log(ERROR, "Query failed: " + sql);
    }

    pool->release_connection(conn);
    return success;
}



std::vector<Resource> ResourceDAO::get_resources(int user_id){
     std::vector<Resource> res;
     msg.clear();
     connection_pool* pool = connection_pool::get_instance();
     MYSQL* conn = pool->get_connection();
 
     if(!conn) {
        msg = std::string("DB connection failed.");
        Logger::get_instance()->log(ERROR, msg);
        return res;
     }

     std::string sql =
         "SELECT id, title, content, is_file FROM resources WHERE user_id=" + std::to_string(user_id);

     
     if(mysql_query(conn, sql.c_str())){
         msg = std::string("Query failed.");
         Logger::get_instance()->log(ERROR, msg);
         pool->release_connection(conn);
         return res;
     }
 
     MYSQL_RES* result = mysql_store_result(conn);
     if (!result){
         msg = std::string("Result not found.");
         Logger::get_instance()->log(ERROR, msg);
         pool->release_connection(conn);
         return res;
     }
     
     MYSQL_ROW row;
     while ((row = mysql_fetch_row(result))) {
        Resource r;
        r.id = atoi(row[0]);
        r.title = row[1] ? row[1] : "";
        r.content = row[2] ? row[2] : "";
        r.is_file = row[3] && atoi(row[3]) != 0;

        res.push_back(r);
    }

     mysql_free_result(result);
     pool->release_connection(conn);
 
     return res;
}


std::optional<Resource> ResourceDAO::get_resource(int user_id, int id){
    msg.clear();
    connection_pool* pool = connection_pool::get_instance();
    MYSQL* conn = pool->get_connection();

    if (!conn) {
        msg = std::string("DB connection failed.");
        Logger::get_instance()->log(ERROR, msg);
        return std::nullopt;
    }

    std::string sql =
        "SELECT id, title, content, is_file FROM resources WHERE user_id=" +
        std::to_string(user_id) + " AND id=" + std::to_string(id);

    if (mysql_query(conn, sql.c_str())) {
        msg = std::string("Query failed.");
        Logger::get_instance()->log(ERROR, msg);
        pool->release_connection(conn);
        return std::nullopt;
    }

    MYSQL_RES* result = mysql_store_result(conn);
    if (!result) {
        msg = std::string("Result not found.");
        Logger::get_instance()->log(ERROR, msg);
        pool->release_connection(conn);
        return std::nullopt;
    }

    MYSQL_ROW row = mysql_fetch_row(result);
    if (!row) {
        msg = std::string("Resource not found.");
        Logger::get_instance()->log(ERROR, msg);
        mysql_free_result(result);
        pool->release_connection(conn);
        return std::nullopt;
    }

    Resource r;
    r.id = atoi(row[0]);
    r.title = row[1] ? row[1] : "";
    r.content = row[2] ? row[2] : "";
    r.is_file = row[3] && atoi(row[3]) != 0;

    mysql_free_result(result);
    pool->release_connection(conn);
    return r;
}


bool ResourceDAO::update_resource(const std::string& sql){
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
        msg = std::string("No change proceeded.");
        Logger::get_instance()->log(ERROR, msg);
    }    
    pool->release_connection(conn);
    return success;
}


bool ResourceDAO::delete_resource(int user_id, int id){
    connection_pool* pool = connection_pool::get_instance();
    MYSQL* conn = pool->get_connection();

    if (!conn) {
        msg = std::string("DB connection failed.");
        return false;
    }
    std::string sql =
        "DELETE FROM resources WHERE id=" + std::to_string(id) + 
        " And user_id=" + std::to_string(user_id);
        
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
