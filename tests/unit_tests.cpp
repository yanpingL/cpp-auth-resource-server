#include "utils/auth_utils.h"

#include <iostream>
#include <set>
#include <string>

namespace {

int failures = 0;

void expect_true(bool condition, const std::string& name) {
    if (condition) {
        std::cout << "[PASS] " << name << '\n';
        return;
    }

    std::cout << "[FAIL] " << name << '\n';
    ++failures;
}

bool is_lower_hex(const std::string& value) {
    for (char c : value) {
        const bool digit = c >= '0' && c <= '9';
        const bool hex_alpha = c >= 'a' && c <= 'f';
        if (!digit && !hex_alpha) {
            return false;
        }
    }
    return true;
}

void test_sha256_known_value() {
    expect_true(
        sha256("123") ==
            "a665a45920422f9d417e4867efdc4fb8a04a1f3fff1fa07e998e86f7f7a27ae3",
        "sha256 hashes known password value");
}

void test_sha256_output_shape() {
    const std::string hash = sha256("hello");

    expect_true(hash.size() == 64, "sha256 returns 64 hex characters");
    expect_true(is_lower_hex(hash), "sha256 returns lowercase hex");
}

void test_token_output_shape() {
    const std::string token = generate_token();

    expect_true(token.size() == 64, "generate_token returns 64 hex characters");
    expect_true(is_lower_hex(token), "generate_token returns lowercase hex");
}

void test_token_uniqueness_smoke() {
    std::set<std::string> tokens;

    for (int i = 0; i < 100; ++i) {
        const std::string token = generate_token();
        expect_true(!token.empty(), "generate_token does not fail");
        tokens.insert(token);
    }

    expect_true(tokens.size() == 100, "generate_token produces unique values in smoke test");
}

} // namespace

int main() {
    test_sha256_known_value();
    test_sha256_output_shape();
    test_token_output_shape();
    test_token_uniqueness_smoke();

    if (failures != 0) {
        std::cout << failures << " unit test expectation(s) failed\n";
        return 1;
    }

    std::cout << "All unit tests passed\n";
    return 0;
}
