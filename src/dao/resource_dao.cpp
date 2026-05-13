#include "resource_dao.h"
#include "dao/dao_util.h"
#include "db/connection_pool.h"
#include "utils/logger.h"

#include <cstdlib>
#include <iostream>

std::string ResourceDAO::msg;

// Executes a resource INSERT statement.
bool ResourceDAO::create_resource(const Resource& resource) {
    msg.clear();

    connection_pool* pool = connection_pool::get_instance();
    PGconn* conn = pool->get_connection();

    if (!conn) {
        msg = std::string("DB connection failed.");
        Logger::get_instance()->log(ERROR, msg);
        return false;
    }

    const char* sql =
        "INSERT INTO resources (user_id, title, content, is_file) "
        "VALUES ($1::int, $2, $3, $4::boolean)";
    std::string user_id = std::to_string(resource.user_id);
    std::string is_file = resource.is_file ? "true" : "false";
    const char* values[] = {
        user_id.c_str(),
        resource.title.c_str(),
        resource.content.c_str(),
        is_file.c_str(),
    };

    PGresult* result = PQexecParams(
        conn,    // DB connection
        sql,     // SQL query string
        4,       // #Parameters
        nullptr, // Parameter types
        values,  // Actual values
        nullptr, // Parameter length
        nullptr, // Parameter formats
        0);      // Return results in text format, not binary
    bool success = DaoUtil::command_ok(conn, result, sql, msg);

    PQclear(result);
    pool->release_connection(conn);
    return success;
}

// Loads all resources owned by one user.
std::vector<Resource> ResourceDAO::get_resources(int user_id) {
    msg.clear();

    std::vector<Resource> res;
    connection_pool* pool = connection_pool::get_instance();
    PGconn* conn = pool->get_connection();

    if (!conn) {
        msg = std::string("DB connection failed.");
        Logger::get_instance()->log(ERROR, msg);
        return res;
    }

    const char* sql =
        "SELECT id, title, content, is_file FROM resources "
        "WHERE user_id=$1::int";
    std::string user_id_value = std::to_string(user_id);
    const char* values[] = {
        user_id_value.c_str(),
    };

    PGresult* result = PQexecParams(
        conn,
        sql,
        1,
        nullptr,
        values,
        nullptr,
        nullptr,
        0);
    if (PQresultStatus(result) != PGRES_TUPLES_OK) {
        msg = std::string("Query failed: ") + PQerrorMessage(conn);
        Logger::get_instance()->log(ERROR, msg + " SQL: " + sql);
        PQclear(result);
        pool->release_connection(conn);
        return res;
    }

    int row_count = PQntuples(result);
    for (int row = 0; row < row_count; ++row) {
        Resource r;
        r.id = std::atoi(PQgetvalue(result, row, 0));
        r.title = DaoUtil::nullable_value(result, row, 1);
        r.content = DaoUtil::nullable_value(result, row, 2);
        r.is_file = DaoUtil::pg_bool_value(result, row, 3);

        res.push_back(r);
    }

    PQclear(result);
    pool->release_connection(conn);

    return res;
}

// Loads one resource by user id and resource id.
std::optional<Resource> ResourceDAO::get_resource(int user_id, int id) {
    msg.clear();

    connection_pool* pool = connection_pool::get_instance();
    PGconn* conn = pool->get_connection();

    if (!conn) {
        msg = std::string("DB connection failed.");
        Logger::get_instance()->log(ERROR, msg);
        return std::nullopt;
    }

    const char* sql =
        "SELECT id, title, content, is_file FROM resources "
        "WHERE user_id=$1::int AND id=$2::int";
    std::string user_id_value = std::to_string(user_id);
    std::string id_value = std::to_string(id);
    const char* values[] = {
        user_id_value.c_str(),
        id_value.c_str(),
    };

    PGresult* result = PQexecParams(
        conn,
        sql,
        2,
        nullptr,
        values,
        nullptr,
        nullptr,
        0);
    if (PQresultStatus(result) != PGRES_TUPLES_OK) {
        msg = std::string("Query failed: ") + PQerrorMessage(conn);
        Logger::get_instance()->log(ERROR, msg + " SQL: " + sql);
        PQclear(result);
        pool->release_connection(conn);
        return std::nullopt;
    }

    if (PQntuples(result) == 0) {
        msg = std::string("Resource not found.");
        Logger::get_instance()->log(ERROR, msg);
        PQclear(result);
        pool->release_connection(conn);
        return std::nullopt;
    }

    Resource r;
    r.id = std::atoi(PQgetvalue(result, 0, 0));
    r.title = DaoUtil::nullable_value(result, 0, 1);
    r.content = DaoUtil::nullable_value(result, 0, 2);
    r.is_file = DaoUtil::pg_bool_value(result, 0, 3);

    PQclear(result);
    pool->release_connection(conn);
    return r;
}

// Executes a resource UPDATE statement and reports whether a row changed.
bool ResourceDAO::update_resource(const Resource& resource) {
    msg.clear();

    connection_pool* pool = connection_pool::get_instance();
    PGconn* conn = pool->get_connection();

    if (!conn) {
        msg = std::string("DB connection failed.");
        Logger::get_instance()->log(ERROR, msg);
        return false;
    }

    const char* sql =
        "UPDATE resources SET title=$1, content=$2 "
        "WHERE user_id=$3::int AND id=$4::int";
    std::string user_id = std::to_string(resource.user_id);
    std::string id = std::to_string(resource.id);
    const char* values[] = {
        resource.title.c_str(),
        resource.content.c_str(),
        user_id.c_str(),
        id.c_str(),
    };

    PGresult* result = PQexecParams(
        conn,
        sql,
        4,
        nullptr,
        values,
        nullptr,
        nullptr,
        0);
    bool success = DaoUtil::command_ok(conn, result, sql, msg);
    if (success && !DaoUtil::affected_rows(result)) {
        success = false;
        msg = std::string("No change proceeded.");
        Logger::get_instance()->log(ERROR, msg);
    }

    PQclear(result);
    pool->release_connection(conn);
    return success;
}

// Deletes one resource owned by the given user.
bool ResourceDAO::delete_resource(int user_id, int id) {
    msg.clear();

    connection_pool* pool = connection_pool::get_instance();
    PGconn* conn = pool->get_connection();

    if (!conn) {
        msg = std::string("DB connection failed.");
        Logger::get_instance()->log(ERROR, msg);
        return false;
    }

    const char* sql =
        "DELETE FROM resources WHERE id=$1::int AND user_id=$2::int";
    std::string id_value = std::to_string(id);
    std::string user_id_value = std::to_string(user_id);
    const char* values[] = {
        id_value.c_str(),
        user_id_value.c_str(),
    };

    Logger::get_instance()->log(DEBUG, std::string("SQL: ") + sql);

    PGresult* result = PQexecParams(
        conn,
        sql,
        2,
        nullptr,
        values,
        nullptr,
        nullptr,
        0);
    bool success = DaoUtil::command_ok(conn, result, sql, msg);
    if (success && !DaoUtil::affected_rows(result)) {
        success = false;
        msg = std::string("Resource not found.");
        Logger::get_instance()->log(ERROR, msg);
    }

    PQclear(result);
    pool->release_connection(conn);
    return success;
}
