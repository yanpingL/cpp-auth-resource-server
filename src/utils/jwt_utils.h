#ifndef JWT_UTILS_H
#define JWT_UTILS_H

#include <optional>
#include <string>

class JwtUtils {
public:
    static std::string create_jwt(int user_id);
    static std::optional<int> verify_jwt_and_get_user_id(const std::string& token);

private:
    static std::string get_secret();
    static std::string get_issuer();
    static int get_expires_seconds();
};

#endif
