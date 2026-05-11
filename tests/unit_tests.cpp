#include "utils/auth_utils.h"

#include <cctype>
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
