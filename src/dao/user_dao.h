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
    static std::optional<User> get_user_by_email(const std::string& email);
};

#endif
