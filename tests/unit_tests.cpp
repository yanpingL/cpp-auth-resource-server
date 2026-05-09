#include "utils/auth_utils.h"

#include <cctype>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void expect_true(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void test_sha256_returns_expected_hash() {
    const std::string hash = sha256("password");

    expect_true(
        hash == "5e884898da28047151d0e56f8dc6292773603d0d6aabbdd62a11ef721d1542d8",
        "sha256(\"password\") should return the expected hash"
    );
}

void test_generate_token_returns_64_hex_characters() {
    const std::string token = generate_token();

    expect_true(token.size() == 64, "generated token should be 64 characters");

    for (char ch : token) {
        expect_true(
            std::isxdigit(static_cast<unsigned char>(ch)) != 0,
            "generated token should contain only hex characters"
        );
    }
}

} // namespace

int main() {
    try {
        test_sha256_returns_expected_hash();
        test_generate_token_returns_64_hex_characters();
    } catch (const std::exception& error) {
        std::cerr << "Unit test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "All unit tests passed\n";
    return 0;
}
