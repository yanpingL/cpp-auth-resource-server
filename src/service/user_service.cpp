#include "user_service.h"
#include "dao/user_dao.h"
#include "utils/auth_utils.h"
#include "utils/jwt_utils.h"

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



// Authenticates a user, creates a JWT bearer token, and returns login JSON.
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
    // Generate signed JWT.
    std::string token = JwtUtils::create_jwt(user.id);
    if (token.empty()) {
        UserDAO::msg = "token generation failed";
        res["error"] = UserDAO::msg;
        return res;
    }

    res["token"] = token;
    res["user_id"] = user.id;
    res["name"] = user.name;

    return res;
}


// Resolves a bearer token to the authenticated user id.
std::optional<int> UserService::get_user_id_from_token(const std::string& token) {
    return JwtUtils::verify_jwt_and_get_user_id(token);
}


json_type UserService::logout(const std::string& token) {
    (void)token;
    json_type res;
    res["status"] = "logout success";
    return res;
}
