#ifndef USER_DAO_H
#define USER_DAO_H

// Data layer

#include <string>
#include <optional>

// simple struct to represent user
struct User{
    int id;
    std::string name;
    std::string email;
    std::string password;
};

class UserDAO {
public:
    static std::string msg;

    static std::optional<User> get_user_by_id(int id);

    static bool delete_user(int id);

    // reuse the existing INSERT SQL
    static bool create_user(const std::string& sql);

    // reuse the existing UPDATE sql
    static bool update_user(const std::string& sql);

    static std::optional<User> get_user_by_email(const std::string& email);

    static bool validate_token(const std::string& token);

    static bool delete_session(const std::string& token);
};




#endif