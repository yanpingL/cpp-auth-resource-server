#include "user_dao.h"
#include "db/connection_pool.h"
#include "utils/logger.h"
#include "cache/redis_client.h"

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

// Creates a login session in Redis and PostgreSQL.
bool UserDAO::create_session(int user_id, const std::string& token, int ttl_seconds) {
    RedisClient::get_instance()->set(
        token,
        std::to_string(user_id),
        ttl_seconds
    );

    connection_pool* pool = connection_pool::get_instance();
    PGconn* conn = pool->get_connection();

    if (!conn) {
        msg = std::string("DB connection failed.");
        Logger::get_instance()->log(ERROR, msg);
        RedisClient::get_instance()->del(token);
        return false;
    }

    std::string sql =
        "INSERT INTO sessions (user_id, token, expires_at) VALUES (" +
        std::to_string(user_id) + ", '" + token + "', NOW() + INTERVAL '" +
        std::to_string(ttl_seconds) + " seconds')";

    Logger::get_instance()->log(DEBUG, "SQL: " + sql);

    PGresult* result = PQexec(conn, sql.c_str());
    bool success = command_ok(conn, result, sql, msg);
    if (!success) {
        RedisClient::get_instance()->del(token);
    }

    PQclear(result);
    pool->release_connection(conn);
    return success;
}

// Removes a session token from Redis and PostgreSQL.
bool UserDAO::delete_session(const std::string& token) {
    RedisClient::get_instance()->del(token);

    connection_pool* pool = connection_pool::get_instance();
    PGconn* conn = pool->get_connection();

    if (!conn) {
        msg = std::string("DB connection failed.");
        Logger::get_instance()->log(ERROR, msg);
        return false;
    }

    std::string sql =
        "DELETE FROM sessions WHERE token='" + token + "'";

    PGresult* result = PQexec(conn, sql.c_str());
    bool success = command_ok(conn, result, sql, msg);

    PQclear(result);
    pool->release_connection(conn);
    return success;
}

// Resolves a valid session token to the owning user id.
std::optional<int> UserDAO::get_user_id_from_token(const std::string& token) {
    std::string uid = RedisClient::get_instance()->get(token);
    if (!uid.empty()) {
        return std::stoi(uid);
    }

    connection_pool* pool = connection_pool::get_instance();
    PGconn* conn = pool->get_connection();

    if (!conn) {
        msg = std::string("DB connection failed.");
        Logger::get_instance()->log(ERROR, msg);
        return std::nullopt;
    }

    std::string sql =
        "SELECT user_id FROM sessions WHERE token='" + token +
        "' AND expires_at > NOW()";

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

    int user_id = std::atoi(PQgetvalue(result, 0, 0));

    RedisClient::get_instance()->set(token, std::to_string(user_id), 3600);

    PQclear(result);
    pool->release_connection(conn);

    return user_id;
}
