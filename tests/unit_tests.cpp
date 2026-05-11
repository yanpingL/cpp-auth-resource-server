#include "utils/auth_utils.h"
#include "utils/env_utils.h"
#include "utils/jwt_utils.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <optional>
#include <string>
#include <unordered_set>

#include <gtest/gtest.h>

TEST(AuthUtilsTest, Sha256ReturnsExpectedHash) {
    const std::string hash = sha256("password");

    EXPECT_EQ(
        "5e884898da28047151d0e56f8dc6292773603d0d6aabbdd62a11ef721d1542d8",
        hash
    );
}

TEST(AuthUtilsTest, Sha256HandlesEmptyInput) {
    const std::string hash = sha256("");

    EXPECT_EQ(
        "e3b0c44298fc1c149afbf4c8996fb924"
        "27ae41e4649b934ca495991b7852b855",
        hash
    );
}

TEST(AuthUtilsTest, Sha256IsDeterministic) {
    const std::string first_hash = sha256("same input");
    const std::string second_hash = sha256("same input");

    EXPECT_EQ(first_hash, second_hash);
}

TEST(AuthUtilsTest, Sha256ChangesWhenInputChanges) {
    const std::string first_hash = sha256("resource-title");
    const std::string second_hash = sha256("resource-title ");

    EXPECT_NE(first_hash, second_hash);
}

TEST(AuthUtilsTest, GenerateTokenReturns64HexCharacters) {
    const std::string token = generate_token();

    ASSERT_EQ(64U, token.size());

    for (char ch : token) {
        EXPECT_TRUE(std::isxdigit(static_cast<unsigned char>(ch)) != 0)
            << "token contains non-hex character: " << ch;
    }
}

TEST(AuthUtilsTest, GenerateTokenReturnsUniqueValues) {
    std::unordered_set<std::string> tokens;

    for (int i = 0; i < 32; ++i) {
        const std::string token = generate_token();

        EXPECT_FALSE(token.empty());
        EXPECT_TRUE(tokens.insert(token).second)
            << "generate_token repeated value: " << token;
    }
}

TEST(EnvUtilsTest, ReturnsFallbackForMissingEnvironmentVariable) {
    unsetenv("UNIT_TEST_MISSING_ENV");

    EXPECT_EQ(
        "fallback",
        EnvUtils::get_env_or_default("UNIT_TEST_MISSING_ENV", "fallback")
    );
    EXPECT_EQ(
        42,
        EnvUtils::get_env_int_or_default("UNIT_TEST_MISSING_ENV", 42)
    );
}

TEST(EnvUtilsTest, ParsesIntegerEnvironmentVariable) {
    setenv("UNIT_TEST_INT_ENV", "123", 1);

    EXPECT_EQ(123, EnvUtils::get_env_int_or_default("UNIT_TEST_INT_ENV", 42));

    unsetenv("UNIT_TEST_INT_ENV");
}

TEST(EnvUtilsTest, ReturnsFallbackForInvalidIntegerEnvironmentVariable) {
    setenv("UNIT_TEST_INT_ENV", "not-a-number", 1);

    EXPECT_EQ(42, EnvUtils::get_env_int_or_default("UNIT_TEST_INT_ENV", 42));

    unsetenv("UNIT_TEST_INT_ENV");
}

class JwtUtilsTest : public ::testing::Test {
protected:
    void SetUp() override {
        setenv("JWT_SECRET", "unit-test-secret-with-enough-length", 1);
        setenv("JWT_ISSUER", "unit-test-webserver", 1);
        setenv("JWT_EXPIRES_SECONDS", "3600", 1);
    }

    void TearDown() override {
        unsetenv("JWT_SECRET");
        unsetenv("JWT_ISSUER");
        unsetenv("JWT_EXPIRES_SECONDS");
    }
};

TEST_F(JwtUtilsTest, CreateJwtReturnsThreePartToken) {
    const std::string token = JwtUtils::create_jwt(7);

    EXPECT_FALSE(token.empty());
    EXPECT_EQ(2U, std::count(token.begin(), token.end(), '.'));
}

TEST_F(JwtUtilsTest, VerifyJwtReturnsUserId) {
    const std::string token = JwtUtils::create_jwt(7);
    const std::optional<int> user_id = JwtUtils::verify_jwt_and_get_user_id(token);

    ASSERT_TRUE(user_id.has_value());
    EXPECT_EQ(7, user_id.value());
}

TEST_F(JwtUtilsTest, CreateJwtReturnsEmptyWhenSecretMissing) {
    unsetenv("JWT_SECRET");

    EXPECT_TRUE(JwtUtils::create_jwt(7).empty());
}

TEST_F(JwtUtilsTest, CreateJwtReturnsEmptyWhenExpiryIsInvalid) {
    setenv("JWT_EXPIRES_SECONDS", "0", 1);

    EXPECT_TRUE(JwtUtils::create_jwt(7).empty());
}

TEST_F(JwtUtilsTest, VerifyJwtRejectsMalformedToken) {
    EXPECT_FALSE(JwtUtils::verify_jwt_and_get_user_id("not-a-jwt").has_value());
}

TEST_F(JwtUtilsTest, VerifyJwtRejectsTokenSignedWithDifferentSecret) {
    const std::string token = JwtUtils::create_jwt(7);
    setenv("JWT_SECRET", "different-unit-test-secret", 1);

    EXPECT_FALSE(JwtUtils::verify_jwt_and_get_user_id(token).has_value());
}

TEST_F(JwtUtilsTest, VerifyJwtRejectsTokenWithDifferentIssuer) {
    const std::string token = JwtUtils::create_jwt(7);
    setenv("JWT_ISSUER", "different-issuer", 1);

    EXPECT_FALSE(JwtUtils::verify_jwt_and_get_user_id(token).has_value());
}

TEST_F(JwtUtilsTest, VerifyJwtRejectsTamperedToken) {
    std::string token = JwtUtils::create_jwt(7);
    ASSERT_FALSE(token.empty());

    token.back() = token.back() == 'a' ? 'b' : 'a';

    EXPECT_FALSE(JwtUtils::verify_jwt_and_get_user_id(token).has_value());
}
