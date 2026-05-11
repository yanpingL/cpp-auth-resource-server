#include "user_dao.h"
#include "db/connection_pool.h"
#include "utils/logger.h"

#include <cstdlib>
#include <iostream>

std::string UserDAO::msg;

namespace {

bool command_ok(PGconn* conn, PGresult* result, const std::string& sql, std::string& msg) {
    bool success = PQresultStatus(result) == PGRES_COMMAND_OK;
    if (!success) {
        msg = std::string("Query failed: ") + PQerrorMessage(conn);
        Logger::get_instance()->log(ERROR, msg + " SQL: " + sql);
    }
    return success;
}

} // namespace

// Creates a user record.
bool UserDAO::create_user(const User& user) {
    connection_pool* pool = connection_pool::get_instance();
    PGconn* conn = pool->get_connection();

    if (!conn) {
        msg = std::string("DB connection failed.");
        Logger::get_instance()->log(ERROR, msg);
        return false;
    }

    std::string sql =
        "INSERT INTO users (name, email, password) VALUES ('" +
        user.name + "', '" +
        user.email + "', '" +
        user.password + "')";

    PGresult* result = PQexec(conn, sql.c_str());
    bool success = command_ok(conn, result, sql, msg);

    PQclear(result);
    pool->release_connection(conn);
    return success;
}

// Loads one user by email for login.
std::optional<User> UserDAO::get_user_by_email(const std::string& email) {
    connection_pool* pool = connection_pool::get_instance();
    PGconn* conn = pool->get_connection();

    if (!conn) {
        msg = std::string("DB connection failed.");
        Logger::get_instance()->log(ERROR, msg);
        return std::nullopt;
    }

    std::string sql =
        "SELECT id, name, email, password FROM users WHERE email='" + email + "'";

    Logger::get_instance()->log(DEBUG, "SQL: " + sql);

    PGresult* result = PQexec(conn, sql.c_str());
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
