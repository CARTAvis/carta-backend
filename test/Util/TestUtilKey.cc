/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>
#include <unordered_map>
#include "Util/Key.h"

using namespace carta;

class RegressionKeyTest : public ::testing::Test {};
TEST_F(RegressionKeyTest, TestIssue1651) {
    std::unordered_map<ChannelKey, std::string> mymap;
    mymap[ChannelKey(13, 100)] = "Z=100, Ptotal";
    mymap[ChannelKey(3, 101)] = "Z=101, Stokes V";

    ASSERT_EQ(mymap.size(), 2);
    ASSERT_EQ(mymap.at(ChannelKey(13, 100)), "Z=100, Ptotal");
    ASSERT_EQ(mymap.at(ChannelKey(3, 101)), "Z=101, Stokes V");
}

template <typename T>
class TwoValueKeyTest : public ::testing::Test {};

using TwoValueKeyTypes = ::testing::Types<CoordinatesKey, ChannelKey, FileRegionKey>;
TYPED_TEST_SUITE(TwoValueKeyTest, TwoValueKeyTypes);

TYPED_TEST(TwoValueKeyTest, TestOperators) {
    ASSERT_EQ(TypeParam(1, 2), TypeParam(1, 2));
    ASSERT_NE(TypeParam(1, 2), TypeParam(2, 1));
}

TYPED_TEST(TwoValueKeyTest, TestTupleRepresentation) {
    ASSERT_EQ(TypeParam(1, 2).as_tuple, TypeParam(1, 2).as_tuple);
    ASSERT_NE(TypeParam(1, 2).as_tuple, TypeParam(2, 1).as_tuple);
    ASSERT_EQ(TypeParam(1, 2).as_tuple, std::tuple(1, 2));
    ASSERT_EQ(TypeParam(2, 1).as_tuple, std::tuple(2, 1));
}

TYPED_TEST(TwoValueKeyTest, TestHash) {
    ASSERT_EQ(std::hash<TypeParam>{}(TypeParam(1, 2)), std::hash<TypeParam>{}(TypeParam(1, 2)));
    ASSERT_NE(std::hash<TypeParam>{}(TypeParam(1, 2)), std::hash<TypeParam>{}(TypeParam(2, 1)));
    ASSERT_EQ(std::hash<TypeParam>{}(TypeParam(1, 2)), 63); // 1 + 2 * 31
    ASSERT_EQ(std::hash<TypeParam>{}(TypeParam(2, 1)), 33); // 2 + 1 * 31
}

class FourValueKeyTest : public ::testing::Test {};
using FRCK = FileRegionChannelKey;

TEST_F(FourValueKeyTest, TestOperators) {
    ASSERT_EQ(FRCK(1, 2, 3, 4), FRCK(1, 2, 3, 4));
    ASSERT_NE(FRCK(1, 2, 3, 4), FRCK(4, 3, 2, 1));
    ASSERT_NE(FRCK(1, 2, 3, 4), FRCK(2, 3, 4, 1));
}

TEST_F(FourValueKeyTest, TestTupleRepresentation) {
    ASSERT_EQ(FRCK(1, 2, 3, 4).as_tuple, FRCK(1, 2, 3, 4).as_tuple);
    ASSERT_NE(FRCK(1, 2, 3, 4).as_tuple, FRCK(4, 3, 2, 1).as_tuple);
    ASSERT_NE(FRCK(1, 2, 3, 4).as_tuple, FRCK(2, 3, 4, 1).as_tuple);
    ASSERT_EQ(FRCK(1, 2, 3, 4).as_tuple, std::tuple(1, 2, 3, 4));
    ASSERT_EQ(FRCK(4, 3, 2, 1).as_tuple, std::tuple(4, 3, 2, 1));
    ASSERT_EQ(FRCK(2, 3, 4, 1).as_tuple, std::tuple(2, 3, 4, 1));
}

TEST_F(FourValueKeyTest, TestHash) {
    ASSERT_EQ(std::hash<FRCK>{}(FRCK(1, 2, 3, 4)), std::hash<FRCK>{}(FRCK(1, 2, 3, 4)));
    ASSERT_NE(std::hash<FRCK>{}(FRCK(1, 2, 3, 4)), std::hash<FRCK>{}(FRCK(4, 3, 2, 1)));
    ASSERT_NE(std::hash<FRCK>{}(FRCK(1, 2, 3, 4)), std::hash<FRCK>{}(FRCK(2, 3, 4, 1)));
    ASSERT_EQ(std::hash<FRCK>{}(FRCK(1, 2, 3, 4)), 122110); // 1 + 2 * 31 + 3 * 31^2 + 4 * 31^3
    ASSERT_EQ(std::hash<FRCK>{}(FRCK(4, 3, 2, 1)), 31810);  // 4 + 3 * 31 + 2 * 31^2 + 1 * 31^3
    ASSERT_EQ(std::hash<FRCK>{}(FRCK(2, 3, 4, 1)), 33730);  // 2 + 3 * 31 + 4 * 31^2 + 1 * 31^3
}
