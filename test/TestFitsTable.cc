/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018 - 2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/
#include <gtest/gtest.h>

#include "Table/Table.h"

#include "CommonTestUtilities.h"

using namespace carta;

class FitsTableTest : public ::testing::Test, public FileFinder {};

TEST_F(FitsTableTest, ParseIvoaExampleHeaderOnly) {
    std::cout << FitsTablePath("test.fits") << std::endl;
    Table table(FitsTablePath("ivoa_example.fits"), true);
    EXPECT_TRUE(table.IsValid());
    EXPECT_EQ(table.NumRows(), 0);
}

TEST_F(FitsTableTest, ParseIvoaExample) {
    Table table(FitsTablePath("ivoa_example.fits"));
    EXPECT_TRUE(table.IsValid());
    EXPECT_EQ(table.NumRows(), 3);
}

TEST_F(FitsTableTest, CorrectFieldCount) {
    Table table(FitsTablePath("ivoa_example.fits"));
    EXPECT_TRUE(table.IsValid());
    EXPECT_EQ(table.NumColumns(), 8);
}

TEST_F(FitsTableTest, CorrectFieldNames) {
    Table table(FitsTablePath("ivoa_example.fits"));
    EXPECT_EQ(table[0]->name, "RA");
    EXPECT_EQ(table[1]->name, "Dec");
    EXPECT_EQ(table[2]->name, "Name");
    EXPECT_EQ(table[3]->name, "RVel");
    EXPECT_EQ(table[4]->name, "e_RVel");
    EXPECT_EQ(table[5]->name, "R");
    EXPECT_EQ(table[6]->name, "BooleanField");
    EXPECT_EQ(table[7]->name, "SingleCharField");
}

TEST_F(FitsTableTest, CorrectFieldUnits) {
    Table table(FitsTablePath("ivoa_example.fits"));
    EXPECT_EQ(table[0]->unit, "deg");
    EXPECT_EQ(table[1]->unit, "deg");
    EXPECT_TRUE(table[2]->unit.empty());
    EXPECT_EQ(table[3]->unit, "km/s");
    EXPECT_EQ(table[4]->unit, "km/s");
    EXPECT_EQ(table[5]->unit, "Mpc");
    EXPECT_TRUE(table[6]->unit.empty());
    EXPECT_TRUE(table[7]->unit.empty());
}

TEST_F(FitsTableTest, CorrectFieldTypes) {
    Table table(FitsTablePath("ivoa_example.fits"));
    EXPECT_EQ(table[0]->data_type, CARTA::Float);
    EXPECT_EQ(table[1]->data_type, CARTA::Float);
    EXPECT_EQ(table[2]->data_type, CARTA::String);
    EXPECT_EQ(table[3]->data_type, CARTA::Int32);
    EXPECT_EQ(table[4]->data_type, CARTA::Int16);
    EXPECT_EQ(table[5]->data_type, CARTA::Float);
    EXPECT_EQ(table[6]->data_type, CARTA::Bool);
    EXPECT_EQ(table[7]->data_type, CARTA::String);
}

TEST_F(FitsTableTest, CorrectFieldSizes) {
    Table table(FitsTablePath("ivoa_example.fits"));
    EXPECT_EQ(table[0]->data_type_size, 4);
    EXPECT_EQ(table[1]->data_type_size, 4);
    EXPECT_EQ(table[2]->data_type_size, 6);
    EXPECT_EQ(table[3]->data_type_size, 4);
    EXPECT_EQ(table[4]->data_type_size, 2);
    EXPECT_EQ(table[5]->data_type_size, 4);
}

TEST_F(FitsTableTest, CorrectNameLookups) {
    Table table(FitsTablePath("ivoa_example.fits"));
    EXPECT_EQ(table["RA"]->name, "RA");
    EXPECT_EQ(table["Dec"]->name, "Dec");
    EXPECT_EQ(table["Name"]->name, "Name");
    EXPECT_EQ(table["RVel"]->name, "RVel");
    EXPECT_EQ(table["e_RVel"]->name, "e_RVel");
    EXPECT_EQ(table["R"]->name, "R");
    EXPECT_EQ(table["dummy"], nullptr);
    EXPECT_EQ(table[""], nullptr);
}

TEST_F(FitsTableTest, CorrectColumnTypes) {
    Table table(FitsTablePath("ivoa_example.fits"));
    EXPECT_NE(DataColumn<float>::TryCast(table["RA"]), nullptr);
    EXPECT_EQ(DataColumn<double>::TryCast(table["RA"]), nullptr);

    EXPECT_NE(DataColumn<std::string>::TryCast(table["Name"]), nullptr);
    EXPECT_EQ(DataColumn<int>::TryCast(table["Name"]), nullptr);

    EXPECT_NE(DataColumn<int>::TryCast(table["RVel"]), nullptr);
    EXPECT_EQ(DataColumn<std::string>::TryCast(table["RVel"]), nullptr);

    EXPECT_NE(DataColumn<int16_t>::TryCast(table["e_RVel"]), nullptr);
    EXPECT_EQ(DataColumn<int>::TryCast(table["e_RVel"]), nullptr);
}

TEST_F(FitsTableTest, CorrectDataValues) {
    Table table(FitsTablePath("ivoa_example.fits"));

    auto& col1_vals = DataColumn<float>::TryCast(table["RA"])->entries;
    EXPECT_EQ(col1_vals.size(), 3);
    EXPECT_FLOAT_EQ(col1_vals[0], 10.68f);
    EXPECT_FLOAT_EQ(col1_vals[1], 287.43f);

    auto& col2_vals = DataColumn<float>::TryCast(table["Dec"])->entries;
    EXPECT_EQ(col2_vals.size(), 3);
    EXPECT_FLOAT_EQ(col2_vals[0], 41.27f);
    EXPECT_FLOAT_EQ(col2_vals[1], -63.85f);

    auto& col3_vals = DataColumn<std::string>::TryCast(table["Name"])->entries;
    EXPECT_EQ(col3_vals.size(), 3);
    EXPECT_EQ(col3_vals[0], "N 224");
    EXPECT_EQ(col3_vals[1], "N 6744");

    auto& col5_vals = DataColumn<int16_t>::TryCast(table["e_RVel"])->entries;
    EXPECT_EQ(col5_vals.size(), 3);
    EXPECT_EQ(col5_vals[0], 5);
    EXPECT_EQ(col5_vals[1], 6);
}

TEST_F(FitsTableTest, FailOnWrongFilterType) {
    Table table(FitsTablePath("ivoa_example.fits"));
    EXPECT_FALSE(table.View().StringFilter(table["dummy"], "N 224"));
    EXPECT_FALSE(table.View().StringFilter(table["RA"], "N 224"));

    EXPECT_FALSE(table.View().NumericFilter(table["dummy"], CARTA::RangeClosed, 0, 100));
    EXPECT_FALSE(table.View().NumericFilter(table["Name"], CARTA::RangeClosed, 0, 100));
}

TEST_F(FitsTableTest, PassOnCorrectFilterType) {
    Table table(FitsTablePath("ivoa_example.fits"));
    EXPECT_TRUE(table.View().StringFilter(table["Name"], "N 224"));
    EXPECT_TRUE(table.View().NumericFilter(table["RA"], CARTA::RangeClosed, 0, 100));
}

TEST_F(FitsTableTest, CaseSensitiveStringFilter) {
    Table table(FitsTablePath("ivoa_example.fits"));

    auto view = table.View();
    view.StringFilter(table["Name"], "N 224");
    EXPECT_EQ(view.NumRows(), 1);
    view.StringFilter(table["Name"], "n 224");
    EXPECT_EQ(view.NumRows(), 0);
    view.StringFilter(table["Name"], "N 598");
    EXPECT_EQ(view.NumRows(), 0);
}

TEST_F(FitsTableTest, CaseInsensitiveStringFilter) {
    Table table(FitsTablePath("ivoa_example.fits"));

    auto view = table.View();
    view.StringFilter(table["Name"], "N 224", true);
    EXPECT_EQ(view.NumRows(), 1);
    view.StringFilter(table["Name"], "n 224", true);
    EXPECT_EQ(view.NumRows(), 1);
    view.StringFilter(table["Name"], "N 598", true);
    EXPECT_EQ(view.NumRows(), 0);
}

TEST_F(FitsTableTest, FailFilterExtractMistypedValues) {
    Table table(FitsTablePath("ivoa_example.fits"));

    auto view = table.View();
    auto double_vals = view.Values<double>(table["RA"]);
    EXPECT_TRUE(double_vals.empty());
    auto string_vals = view.Values<std::string>(table["RA"]);
    EXPECT_TRUE(string_vals.empty());

    view.StringFilter(table["Name"], "N 6744");
    auto float_vals = view.Values<float>(table["Name"]);
    EXPECT_TRUE(float_vals.empty());
}

TEST_F(FitsTableTest, FilterExtractValues) {
    Table table(FitsTablePath("ivoa_example.fits"));

    auto view = table.View();
    view.NumericFilter(table["RA"], CARTA::GreaterOrEqual, 10);
    auto string_vals = view.Values<std::string>(table["Name"]);
    EXPECT_EQ(string_vals.size(), 3);
    EXPECT_EQ(string_vals[0], "N 224");

    view.StringFilter(table["Name"], "N 6744");
    auto float_vals = view.Values<float>(table["RA"]);
    EXPECT_EQ(float_vals.size(), 1);
    EXPECT_FLOAT_EQ(float_vals[0], 287.43f);
}

TEST_F(FitsTableTest, NumericFilterEqual) {
    Table table(FitsTablePath("ivoa_example.fits"));
    auto view = table.View();
    view.NumericFilter(table["RA"], CARTA::Equal, 287.43);
    EXPECT_EQ(view.NumRows(), 1);
    view.Reset();
    view.NumericFilter(table["e_RVel"], CARTA::Equal, 3);
    EXPECT_EQ(view.NumRows(), 1);
}

TEST_F(FitsTableTest, NumericFilterNotEqual) {
    Table table(FitsTablePath("ivoa_example.fits"));
    auto view = table.View();
    view.NumericFilter(table["RA"], CARTA::NotEqual, 287.43);
    EXPECT_EQ(view.NumRows(), 2);
    view.Reset();
    view.NumericFilter(table["e_RVel"], CARTA::NotEqual, 3);
    EXPECT_EQ(view.NumRows(), 2);
}

TEST_F(FitsTableTest, NumericFilterGreater) {
    Table table(FitsTablePath("ivoa_example.fits"));

    auto view = table.View();
    view.NumericFilter(table["RA"], CARTA::GreaterOrEqual, 10);
    EXPECT_EQ(view.NumRows(), 3);
    view.NumericFilter(table["RA"], CARTA::GreaterOrEqual, 11);
    EXPECT_EQ(view.NumRows(), 2);
    view.NumericFilter(table["RA"], CARTA::GreaterOrEqual, 300);
    EXPECT_EQ(view.NumRows(), 0);
}

TEST_F(FitsTableTest, NumericFilterLesser) {
    Table table(FitsTablePath("ivoa_example.fits"));

    auto view = table.View();
    view.NumericFilter(table["RA"], CARTA::LessorOrEqual, 300);
    EXPECT_EQ(view.NumRows(), 3);
    view.NumericFilter(table["RA"], CARTA::LessorOrEqual, 11);
    EXPECT_EQ(view.NumRows(), 1);
    view.NumericFilter(table["RA"], CARTA::LessorOrEqual, 10);
    EXPECT_EQ(view.NumRows(), 0);
}

TEST_F(FitsTableTest, NumericFilterRange) {
    Table table(FitsTablePath("ivoa_example.fits"));

    auto view = table.View();
    view.NumericFilter(table["RA"], CARTA::RangeClosed, 10, 300);
    EXPECT_EQ(view.NumRows(), 3);
    view.NumericFilter(table["RA"], CARTA::RangeClosed, 11, 300);
    EXPECT_EQ(view.NumRows(), 2);
    view.NumericFilter(table["RA"], CARTA::RangeClosed, 11, 14);
    EXPECT_EQ(view.NumRows(), 0);
}

TEST_F(FitsTableTest, BooleanFilterEqual) {
    Table table(FitsTablePath("ivoa_example.fits"));
    auto view = table.View();
    view.NumericFilter(table["BooleanField"], CARTA::Equal, 1);
    EXPECT_EQ(view.NumRows(), 2);
    view.Reset();
    view.NumericFilter(table["BooleanField"], CARTA::Equal, 0);
    EXPECT_EQ(view.NumRows(), 1);
}

TEST_F(FitsTableTest, BooleanFilterNotEqual) {
    Table table(FitsTablePath("ivoa_example.fits"));
    auto view = table.View();
    view.NumericFilter(table["BooleanField"], CARTA::NotEqual, 0);
    EXPECT_EQ(view.NumRows(), 2);
    view.Reset();
    view.NumericFilter(table["BooleanField"], CARTA::NotEqual, 1);
    EXPECT_EQ(view.NumRows(), 1);
}

TEST_F(FitsTableTest, FailSortMissingColummn) {
    Table table(FitsTablePath("ivoa_example.fits"));

    auto view = table.View();
    EXPECT_FALSE(view.SortByColumn(nullptr));
}

TEST_F(FitsTableTest, SortNumericAscending) {
    Table table(FitsTablePath("ivoa_example.fits"));

    auto view = table.View();
    EXPECT_TRUE(view.SortByColumn(table["RA"]));
    auto vals = view.Values<float>(table["RA"]);
    EXPECT_FLOAT_EQ(vals[0], 10.68f);
    EXPECT_FLOAT_EQ(vals[1], 23.48f);
    EXPECT_FLOAT_EQ(vals[2], 287.43f);
}

TEST_F(FitsTableTest, SortNumericDescending) {
    Table table(FitsTablePath("ivoa_example.fits"));

    auto view = table.View();
    EXPECT_TRUE(view.SortByColumn(table["RA"], false));
    auto vals = view.Values<float>(table["RA"]);
    EXPECT_FLOAT_EQ(vals[0], 287.43f);
    EXPECT_FLOAT_EQ(vals[1], 23.48f);
    EXPECT_FLOAT_EQ(vals[2], 10.68f);
}

TEST_F(FitsTableTest, SortNumericSubset) {
    Table table(FitsTablePath("ivoa_example.fits"));

    // Ascending sort
    auto view = table.View();
    view.NumericFilter(table["RA"], CARTA::RangeClosed, 11, 300);
    EXPECT_TRUE(view.SortByColumn(table["RA"]));
    auto vals = view.Values<float>(table["RA"]);
    EXPECT_FLOAT_EQ(vals[0], 23.48f);
    EXPECT_FLOAT_EQ(vals[1], 287.43f);
}

TEST_F(FitsTableTest, SortStringAscending) {
    Table table(FitsTablePath("ivoa_example.fits"));

    auto view = table.View();
    EXPECT_TRUE(view.SortByColumn(table["Name"]));
    auto vals = view.Values<std::string>(table["Name"]);
    EXPECT_EQ(vals[0], "N 224");
    EXPECT_EQ(vals[1], "N 598");
    EXPECT_EQ(vals[2], "N 6744");
}

TEST_F(FitsTableTest, SortStringDescending) {
    Table table(FitsTablePath("ivoa_example.fits"));

    auto view = table.View();
    EXPECT_TRUE(view.SortByColumn(table["Name"], false));
    auto vals = view.Values<std::string>(table["Name"]);
    EXPECT_EQ(vals[0], "N 6744");
    EXPECT_EQ(vals[1], "N 598");
    EXPECT_EQ(vals[2], "N 224");
}

TEST_F(FitsTableTest, SortStringSubset) {
    Table table(FitsTablePath("ivoa_example.fits"));

    // Ascending sort
    auto view = table.View();
    view.NumericFilter(table["RA"], CARTA::RangeClosed, 11, 300);
    EXPECT_TRUE(view.SortByColumn(table["Name"]));
    auto vals = view.Values<std::string>(table["Name"]);
    EXPECT_EQ(vals[0], "N 598");
    EXPECT_EQ(vals[1], "N 6744");
}

TEST_F(FitsTableTest, ParseArrayFile) {
    Table table(FitsTablePath("array_types.fits"));
    EXPECT_TRUE(table.IsValid());
    EXPECT_EQ(table.NumRows(), 3);
}

TEST_F(FitsTableTest, IgnoreArrayTypes) {
    Table table(FitsTablePath("array_types.fits"));
    EXPECT_EQ(table["FixedArray"]->data_type, CARTA::UnsupportedType);
    EXPECT_EQ(table["BoundedArray"]->data_type, CARTA::UnsupportedType);
    EXPECT_EQ(table["UnboundedArray"]->data_type, CARTA::UnsupportedType);
    EXPECT_EQ(table["FixedArray2D"]->data_type, CARTA::UnsupportedType);
    EXPECT_EQ(table["BoundedArray2D"]->data_type, CARTA::UnsupportedType);
    EXPECT_EQ(table["UnboundedArray2D"]->data_type, CARTA::UnsupportedType);
}

TEST_F(FitsTableTest, CorrectScalarData) {
    Table table(FitsTablePath("array_types.fits"));
    auto& scalar1_vals = DataColumn<float>::TryCast(table["Scalar1"])->entries;
    auto& scalar2_vals = DataColumn<float>::TryCast(table["Scalar2"])->entries;
    EXPECT_FLOAT_EQ(scalar1_vals[0], 1.0f);
    EXPECT_FLOAT_EQ(scalar1_vals[1], 2.0f);
    EXPECT_FLOAT_EQ(scalar1_vals[2], 3.0f);
    EXPECT_FLOAT_EQ(scalar2_vals[0], 2.0f);
    EXPECT_FLOAT_EQ(scalar2_vals[1], 4.0f);
    EXPECT_FLOAT_EQ(scalar2_vals[2], 6.0f);
}
