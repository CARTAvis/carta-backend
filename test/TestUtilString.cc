#include <gtest/gtest.h>
#include <vector>
#include <string>
#include "Util/String.h"

TEST(StringUtilTest, SplitString_Basic) {
    std::string input = "apple,banana,carrot";
    std::vector<std::string> parts;
    SplitString(input, ',', parts);

    std::vector<std::string> expected = {"apple", "banana", "carrot"};
    EXPECT_EQ(parts, expected);
}

TEST(StringUtilTest, SplitString_EmptyInput) {
    std::string input = "";
    std::vector<std::string> parts;
    SplitString(input, ',', parts);

    EXPECT_TRUE(parts.empty());
}

TEST(StringUtilTest, SplitString_HandlesCarriageReturn) {
    std::string input = "apple,banana,\r";
    std::vector<std::string> parts;
    SplitString(input, ',', parts);

    std::vector<std::string> expected = {"apple", "banana", ""};
    EXPECT_EQ(parts, expected);
}

TEST(StringUtilTest, HasSuffix_CaseSensitive) {
    EXPECT_TRUE(HasSuffix("filename.txt", ".txt", true));
    EXPECT_FALSE(HasSuffix("filename.TXT", ".txt", true));
}

TEST(StringUtilTest, HasSuffix_CaseInsensitive) {
    EXPECT_TRUE(HasSuffix("filename.TXT", ".txt", false));
}

TEST(StringUtilTest, HasSuffix_NeedleLongerThanHaystack) {
    EXPECT_FALSE(HasSuffix("short", "longer", true));
}

TEST(StringUtilTest, ConstantTimeStringCompare_EqualStrings) {
    EXPECT_TRUE(ConstantTimeStringCompare("test123", "test123"));
}

TEST(StringUtilTest, ConstantTimeStringCompare_DifferentStrings) {
    EXPECT_FALSE(ConstantTimeStringCompare("test123", "test124"));
}

TEST(StringUtilTest, SafeStringEscape_EscapesSpecialCharacters) {
    EXPECT_EQ(SafeStringEscape("hello world!"), "hello%20world%21");
    // EXPECT_EQ(SafeStringEscape("email@example.com"), "email@example.com");
}

TEST(StringUtilTest, SafeStringUnescape_UnescapesProperly) {
    EXPECT_EQ(SafeStringUnescape("hello%20world%21"), "hello world!");
    EXPECT_EQ(SafeStringUnescape("email@example.com"), "email@example.com");
}

TEST(StringUtilTest, StringToInt_ValidNumber) {
    int value;
    EXPECT_TRUE(StringToInt("1234", value));
    EXPECT_EQ(value, 1234);
}

TEST(StringUtilTest, StringToInt_InvalidInput) {
    int value;
    EXPECT_FALSE(StringToInt("abc", value));
}

TEST(StringUtilTest, StringToInt_OutOfRange) {
    int value;
    EXPECT_FALSE(StringToInt("999999999999999999999", value));
}
