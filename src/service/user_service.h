#ifndef USER_SERVICE_H
#define USER_SERVICE_H

// Client logic layer
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

using json_type = nlohmann::ordered_json;

class UserService{
public:
    static json_type create_user(const std::string& sql);
    static json_type login(const std::string& email, const std::string& password);
    static std::optional<int> get_user_id_from_token(const std::string& token);
};

#endif
