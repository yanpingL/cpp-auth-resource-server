#include "user_service.h"
#include "user_dao.h"

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

