#include "user_service.h"
#include "dao/user_dao.h"
#include "utils/auth_utils.h"

// Creates a user and returns a JSON status object.
json_type UserService::create_user(const UserInfo& Info) {
    // First hash the password
    std::string pass = sha256(Info.password);

    User newUser;
    newUser.name = Info.name;
    newUser.email = Info.email;
    newUser.password = pass;
    bool ok = UserDAO::create_user(newUser);

    json_type res;

    if (!ok) {
        res["error"] = UserDAO::msg;
    } else {
        res["status"] = "created";
    }
    return res;
}



// Authenticates a user, creates a session token, and returns login JSON.
json_type UserService::login(const UserInfo& Info) {

    auto user_opt = UserDAO::get_user_by_email(Info.email);

    json_type res;

    if (!user_opt.has_value()) {
        res["error"] = UserDAO::msg;
        return res;
    }

    const auto& user = user_opt.value();
    // Compare password hash value
    if (user.password != sha256(Info.password)) {
        UserDAO::msg = "wrong password";
        res["error"] = UserDAO::msg;
        return res;
    }
    // Generate Token
    std::string token = generate_token();
    if (token.empty()) {
        UserDAO::msg = "token generation failed";
        res["error"] = UserDAO::msg;
        return res;
    }
    // Create user session
    if (!UserDAO::create_session(user.id, token, 3600)) {
        res["error"] = UserDAO::msg;
        return res;
    }

    res["token"] = token;
    res["user_id"] = user.id;

    return res;
}


// Resolves a bearer token to the authenticated user id.
std::optional<int> UserService::get_user_id_from_token(const std::string& token) {
    return UserDAO::get_user_id_from_token(token);
}


json_type UserService::logout(const std::string& token) {
    bool result = UserDAO::delete_session(token);
    json_type res;
    if (!result){
        res["error"] = UserDAO::msg;
    } else {
        res["status"] = "Logout Succed";
    }
    return res;
}
