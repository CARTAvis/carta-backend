/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>
#include <string>
#include <vector>
#include "Util/Casacore.h"
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

TEST(StringUtilTest, StringCompare) {
    EXPECT_TRUE(ConstantTimeStringCompare("hello world", "hello world"));
    EXPECT_FALSE(ConstantTimeStringCompare("hello w1rld", "hello world"));
    EXPECT_FALSE(ConstantTimeStringCompare("hello w1rld", "hello w2rld"));
    EXPECT_FALSE(ConstantTimeStringCompare("hello w", "hello world"));
    EXPECT_TRUE(ConstantTimeStringCompare("", ""));
    EXPECT_FALSE(ConstantTimeStringCompare("hello world", ""));
    EXPECT_FALSE(ConstantTimeStringCompare("", "hello world"));
}

TEST(StringUtilTest, HasSuffixCaseSensitive) {
    EXPECT_TRUE(HasSuffix("test.fits", ".fits", true));
    EXPECT_FALSE(HasSuffix("test.FITS", ".fits", true));
    EXPECT_FALSE(HasSuffix("test.fits", ".FITS", true));
    EXPECT_FALSE(HasSuffix("test.fits", ".xml", true));
    EXPECT_TRUE(HasSuffix("test.fits.gz", ".fits.gz", true));
    EXPECT_FALSE(HasSuffix("test.fits.gz", ".fits", true));
}

TEST(StringUtilTest, HasSuffixCaseInsensitive) {
    EXPECT_TRUE(HasSuffix("test.fits", ".fits"));
    EXPECT_TRUE(HasSuffix("test.FITS", ".fits"));
    EXPECT_TRUE(HasSuffix("test.fits", ".FITS"));
    EXPECT_FALSE(HasSuffix("test.fits", ".xml"));
    EXPECT_TRUE(HasSuffix("test.fits.gz", ".fits.gz"));
    EXPECT_FALSE(HasSuffix("test.fits.gz", ".fits"));
}

TEST(ParseHistoryBeamHeaderTest, ValidHistoryBeamFormat) {
    std::string header = "HISTORY RESTOR Beam = 2.000E+00 x 1.800E+00 arcsec, pa = 8.000E+01 degrees";
    std::string bmaj, bmin, bpa;

    EXPECT_TRUE(ParseHistoryBeamHeader(header, bmaj, bmin, bpa));
    EXPECT_EQ(bmaj, "2.000E+00arcsec");
    EXPECT_EQ(bmin, "1.800E+00arcsec");
    EXPECT_EQ(bpa, "8.000E+01deg");
}

TEST(ParseHistoryBeamHeaderTest, InvalidHistoryBeamFormat) {
    std::string header = "Invalid format";
    std::string bmaj, bmin, bpa;

    EXPECT_FALSE(ParseHistoryBeamHeader(header, bmaj, bmin, bpa));
}

TEST(ParseHistoryBeamHeaderTest, ValidGaussianBeam) {
    casacore::GaussianBeam beam(casacore::Quantity(1.5, "arcsec"), casacore::Quantity(1.2, "arcsec"), casacore::Quantity(45, "deg"));

    std::string formatted = FormatBeam(beam);
    EXPECT_NE(formatted.find("major: 1.500000 arcsec"), std::string::npos);
    EXPECT_NE(formatted.find("minor: 1.200000 arcsec"), std::string::npos);
    EXPECT_NE(formatted.find("pa: 45.000000 deg"), std::string::npos);
}
