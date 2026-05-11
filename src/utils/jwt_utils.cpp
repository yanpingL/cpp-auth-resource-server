#include "utils/jwt_utils.h"

#include "utils/env_utils.h"

#include <jwt-cpp/traits/nlohmann-json/defaults.h>

#include <chrono>
#include <cstdlib>

std::string JwtUtils::get_secret() {
    return EnvUtils::get_env_or_default("JWT_SECRET", "");
}

std::string JwtUtils::get_issuer() {
    return EnvUtils::get_env_or_default("JWT_ISSUER", "webserver");
}

int JwtUtils::get_expires_seconds() {
    return EnvUtils::get_env_int_or_default("JWT_EXPIRES_SECONDS", 3600);
}

std::string JwtUtils::create_jwt(int user_id) {
    const std::string secret = get_secret();
    const int expires_seconds = get_expires_seconds();
    if (secret.empty() || expires_seconds <= 0) {
        return "";
    }

    const auto now = std::chrono::system_clock::now();

    return jwt::create()
        .set_issuer(get_issuer())
        .set_subject(std::to_string(user_id))
        .set_issued_at(now)
        .set_expires_at(now + std::chrono::seconds(expires_seconds))
        .sign(jwt::algorithm::hs256{secret});
}

std::optional<int> JwtUtils::verify_jwt_and_get_user_id(const std::string& token) {
    const std::string secret = get_secret();
    if (secret.empty() || token.empty()) {
        return std::nullopt;
    }

    try {
        const auto decoded = jwt::decode(token);
        const std::string issuer = get_issuer();

        jwt::verify()
            .allow_algorithm(jwt::algorithm::hs256{secret})
            .with_issuer(issuer)
            .verify(decoded);

        const std::string subject = decoded.get_subject();
        char* end = nullptr;
        long user_id = std::strtol(subject.c_str(), &end, 10);
        if (end == subject.c_str() || *end != '\0' || user_id <= 0) {
            return std::nullopt;
        }

        return static_cast<int>(user_id);
    } catch (...) {
        return std::nullopt;
    }
}
