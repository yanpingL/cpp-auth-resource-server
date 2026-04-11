#include "user_service.h"
#include "user_dao.h"
#include "auth_utils.h"
#include "redis_client.h"

json_type UserService::get_user(int id) {
    auto user_opt = UserDAO::get_user_by_id(id);

    json_type res;

    if (!user_opt.has_value()) {
        res["error"] = UserDAO::msg;
        return res;
    }

    const auto& user = user_opt.value();

    res["id"] = user.id;
    res["name"] = user.name;
    res["email"] = user.email;

    return res;
}



json_type UserService::delete_user(int id) {
    bool ok = UserDAO::delete_user(id);

    json_type res;

    if (!ok) {
        res["error"] = UserDAO::msg;
    } else {
        res["status"] = "deleted";
    }

    return res;
}



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



json_type UserService::update_user(const std::string& sql) {
    bool ok = UserDAO::update_user(sql);

    json_type res;

    if (!ok) {
        res["error"] = UserDAO::msg;
    } else {
        res["status"] = "updated";
    }

    return res;
}



json_type UserService::login(const std::string& email, const std::string& password) {

    auto user_opt = UserDAO::get_user_by_email(email);

    json_type res;

    if (!user_opt.has_value()) {
        res["error"] = "user not found";
        return res;
    }

    const auto& user = user_opt.value();

    // HASH compare
    if (user.password != sha256(password)) {
        res["error"] = "wrong password";
        return res;
    }

    // generate token (simple version)
    std::string token = generate_token();

    // Redis store (main)
    RedisClient::get_instance()->set(
        token,
        std::to_string(user.id),
        3600   // 1 hour expiration
    );

    // store session in DB (backup)
    std::string sql =
        "INSERT INTO sessions (user_id, token, expires_at) VALUES (" +
        std::to_string(user.id) + ", '" + token +"', NOW() + INTERVAL 1 HOUR)";
    
    // add info to sessions table
    UserDAO::create_user(sql);  // reuse existing function

    res["token"] = token;
    res["user_id"] = user.id;

    return res;
}

