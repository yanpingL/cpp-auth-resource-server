#ifndef USER_SERVICE_H
#define USER_SERVICE_H

// Client logic layer
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

struct UserInfo{
    int id;
    std::string name;
    std::string email;
    std::string password;
};



using json_type = nlohmann::ordered_json;

class UserService{
public:
    static json_type create_user(const UserInfo& Info);
    static json_type login(const UserInfo& Info);
    static std::optional<int> get_user_id_from_token(const std::string& token);
    static json_type logout(const std::string& token);
};

#endif
