#ifndef USER_DAO_H
#define USER_DAO_H

#include <string>
#include <optional>

struct User{
    int id;
    std::string name;
    std::string email;
    std::string password;
};

class UserDAO {
public:
    static std::string msg;
    
    static bool create_user(const User& user);
    static std::optional<User> get_user_by_id(int id);
    static std::optional<User> get_user_by_email(const std::string& email);
    static bool update_user(const std::string& sql);
    static bool delete_user(int id);

    static bool create_session(int user_id, const std::string& token, int ttl_seconds);
    static bool validate_token(const std::string& token);
    static bool delete_session(const std::string& token);
    static std::optional<int> get_user_id_from_token(const std::string& token);
};

#endif
