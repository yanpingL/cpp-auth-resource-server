#ifndef AUTH_UTILS_H
#define AUTH_UTILS_H

#include <string>
#include <sstream>
#include <openssl/sha.h>
#include <cstdlib>

// ===== SHA256 =====
inline std::string sha256(const std::string& input) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256((unsigned char*)input.c_str(), input.size(), hash);

    std::stringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        ss << std::hex << (int)hash[i];
    }
    return ss.str();
}

// ===== TOKEN =====
inline std::string generate_token() {
    static const char alphanum[] =
        "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

    std::string token;
    for (int i = 0; i < 32; ++i) {
        token += alphanum[rand() % (sizeof(alphanum) - 1)];
    }
    return token;
}

#endif