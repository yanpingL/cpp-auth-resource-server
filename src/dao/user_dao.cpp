#include "user_dao.h"
#include "dao/dao_util.h"
#include "db/connection_pool.h"
#include "utils/logger.h"

#include <cstdlib>
#include <iostream>

std::string UserDAO::msg;

// Creates a user record.
bool UserDAO::create_user(const User& user) {
    msg.clear();

    connection_pool* pool = connection_pool::get_instance();
    PGconn* conn = pool->get_connection();

    if (!conn) {
        msg = std::string("DB connection failed.");
        Logger::get_instance()->log(ERROR, msg);
        return false;
    }

    const char* sql =
        "INSERT INTO users (name, email, password) VALUES ($1, $2, $3)";
    const char* values[] = {
        user.name.c_str(),
        user.email.c_str(),
        user.password.c_str(),
    };

    PGresult* result = PQexecParams(
        conn,
        sql,
        3,
        nullptr,
        values,
        nullptr,
        nullptr,
        0);
    bool success = DaoUtil::command_ok(conn, result, sql, msg);

    PQclear(result);
    pool->release_connection(conn);
    return success;
}

// Loads one user by email for login.
std::optional<User> UserDAO::get_user_by_email(const std::string& email) {
    msg.clear();

    connection_pool* pool = connection_pool::get_instance();
    PGconn* conn = pool->get_connection();

    if (!conn) {
        msg = std::string("DB connection failed.");
        Logger::get_instance()->log(ERROR, msg);
        return std::nullopt;
    }

    const char* sql =
        "SELECT id, name, email, password FROM users WHERE email=$1";
    const char* values[] = {
        email.c_str(),
    };

    Logger::get_instance()->log(DEBUG, std::string("SQL: ") + sql);

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
        return std::nullopt;
    }

    if (PQntuples(result) == 0) {
        msg = "User not found";
        Logger::get_instance()->log(ERROR, msg);
        PQclear(result);
        pool->release_connection(conn);
        return std::nullopt;
    }

    User user;
    user.id = std::atoi(PQgetvalue(result, 0, 0));
    user.name = PQgetvalue(result, 0, 1);
    user.email = PQgetvalue(result, 0, 2);
    user.password = PQgetvalue(result, 0, 3);

    PQclear(result);
    pool->release_connection(conn);

    return user;
}
