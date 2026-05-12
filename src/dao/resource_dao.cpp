#include "resource_dao.h"
#include "db/connection_pool.h"
#include "utils/logger.h"

#include <cstdlib>
#include <iostream>

std::string ResourceDAO::msg;

namespace {

bool command_ok(PGconn* conn, PGresult* result, const std::string& sql, std::string& msg) {
    bool success = PQresultStatus(result) == PGRES_COMMAND_OK;
    if (!success) {
        msg = std::string("Query failed: ") + PQerrorMessage(conn);
        Logger::get_instance()->log(ERROR, msg + " SQL: " + sql);
    }
    return success;
}

bool affected_rows(PGresult* result) {
    const char* rows = PQcmdTuples(result);
    return rows != nullptr && rows[0] != '\0' && std::atoi(rows) > 0;
}

bool pg_bool_value(PGresult* result, int row, int col) {
    const char* value = PQgetvalue(result, row, col);
    return value != nullptr && (value[0] == 't' || value[0] == '1');
}

std::string nullable_value(PGresult* result, int row, int col) {
    return PQgetisnull(result, row, col) ? "" : PQgetvalue(result, row, col);
}

} // namespace

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

    std::string sql =
        "INSERT INTO resources (user_id, title, content, is_file) VALUES (" +
        std::to_string(resource.user_id) + ", '" +
        resource.title + "', '" +
        resource.content + "', " +
        (resource.is_file ? "TRUE" : "FALSE") + ")";

    PGresult* result = PQexec(conn, sql.c_str());
    bool success = command_ok(conn, result, sql, msg);

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

    std::string sql =
        "SELECT id, title, content, is_file FROM resources WHERE user_id=" +
        std::to_string(user_id);

    PGresult* result = PQexec(conn, sql.c_str());
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
        r.title = nullable_value(result, row, 1);
        r.content = nullable_value(result, row, 2);
        r.is_file = pg_bool_value(result, row, 3);

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

    std::string sql =
        "SELECT id, title, content, is_file FROM resources WHERE user_id=" +
        std::to_string(user_id) + " AND id=" + std::to_string(id);

    PGresult* result = PQexec(conn, sql.c_str());
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
    r.title = nullable_value(result, 0, 1);
    r.content = nullable_value(result, 0, 2);
    r.is_file = pg_bool_value(result, 0, 3);

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

    std::string sql =
        "UPDATE resources SET title='" +
        resource.title + "', content='" +
        resource.content + "', is_file=" +
        (resource.is_file ? "TRUE" : "FALSE") +
        " WHERE user_id=" + std::to_string(resource.user_id) +
        " AND id=" + std::to_string(resource.id);

    PGresult* result = PQexec(conn, sql.c_str());
    bool success = command_ok(conn, result, sql, msg);
    if (success && !affected_rows(result)) {
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

    std::string sql =
        "DELETE FROM resources WHERE id=" + std::to_string(id) +
        " AND user_id=" + std::to_string(user_id);

    Logger::get_instance()->log(DEBUG, "SQL: " + sql);

    PGresult* result = PQexec(conn, sql.c_str());
    bool success = command_ok(conn, result, sql, msg);
    if (success && !affected_rows(result)) {
        success = false;
        msg = std::string("Resource not found.");
        Logger::get_instance()->log(ERROR, msg);
    }

    PQclear(result);
    pool->release_connection(conn);
    return success;
}
