#include "user_service.h"
#include "dao/user_dao.h"
#include "utils/auth_utils.h"
#include "cache/redis_client.h"

// Creates a user and returns a JSON status object.
json_type UserService::create_user(const std::string& sql) {
    bool ok = UserDAO::create_user(sql);

    json_type res;

    if (!ok) {
        res["error"] = UserDAO::msg;
    } else {
        res["status"] = "created";
    }

    return res;
}



// Authenticates a user, creates a session token, and returns login JSON.
json_type UserService::login(const std::string& email, const std::string& password) {

    auto user_opt = UserDAO::get_user_by_email(email);

    json_type res;

    if (!user_opt.has_value()) {
        res["error"] = "user not found";
        return res;
    }

    const auto& user = user_opt.value();

    if (user.password != sha256(password)) {
        res["error"] = "wrong password";
        return res;
    }

    std::string token = generate_token();
    if (token.empty()) {
        res["error"] = "token generation failed";
        return res;
    }

    // Cache the session in Redis and persist it in MySQL as fallback.
    RedisClient::get_instance()->set(
        token,
        std::to_string(user.id),
        3600
    );

    std::string sql =
        "INSERT INTO sessions (user_id, token, expires_at) VALUES (" +
        std::to_string(user.id) + ", '" + token +"', NOW() + INTERVAL 1 HOUR)";

    UserDAO::create_user(sql);

    res["token"] = token;
    res["user_id"] = user.id;

    return res;
}

// Resolves a bearer token to the authenticated user id.
std::optional<int> UserService::get_user_id_from_token(const std::string& token) {
    return UserDAO::get_user_id_from_token(token);
}
