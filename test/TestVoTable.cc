/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>

#include "Table/Table.h"
#include "Util.h"

#ifdef _BOOST_FILESYSTEM_
#include <boost/filesystem.hpp>
namespace fs = boost::filesystem;
#else
#include <filesystem>
namespace fs = std::filesystem;
#endif

using namespace std;
using namespace carta;

class VoTableTest : public ::testing::Test {
public:
    static string ImagePath(const string& filename) {
        string path_string;
        fs::path path;
        if (FindExecutablePath(path_string)) {
            path = fs::path(path_string).parent_path();
        } else {
            path = fs::current_path();
        }
        return (path / "data/tables/xml" / filename).string();
    }
};

TEST_F(VoTableTest, FailOnEmptyFilename) {
    Table table("");
    EXPECT_FALSE(table.IsValid());
}

TEST_F(VoTableTest, FailOnEmptyFilenameHeaderOnly) {
    Table table_header_only("", true);
    EXPECT_FALSE(table_header_only.IsValid());
}

TEST_F(VoTableTest, FailOnMissingResource) {
    Table table(ImagePath("no_resource.xml"));
    EXPECT_FALSE(table.IsValid());
}

TEST_F(VoTableTest, FailOnMissingTable) {
    Table table(ImagePath("no_table.xml"));
    EXPECT_FALSE(table.IsValid());
}

TEST_F(VoTableTest, FailOnMissingData) {
    Table table(ImagePath("no_data.xml"));
    EXPECT_FALSE(table.IsValid());
}

TEST_F(VoTableTest, ParseMissingDataHeaderOnly) {
    Table table(ImagePath("empty_data.xml"), true);
    EXPECT_TRUE(table.IsValid());
    EXPECT_EQ(table.NumRows(), 0);
}

TEST_F(VoTableTest, ParseMissingData) {
    Table table(ImagePath("empty_data.xml"));
    EXPECT_TRUE(table.IsValid());
    EXPECT_EQ(table.NumRows(), 0);
}

TEST_F(VoTableTest, ParseIvoaExampleHeaderOnly) {
    Table table(ImagePath("ivoa_example.xml"), true);
    EXPECT_TRUE(table.IsValid());
    EXPECT_EQ(table.NumRows(), 0);
}

TEST_F(VoTableTest, ParseIvoaExample) {
    Table table(ImagePath("ivoa_example.xml"));
    EXPECT_TRUE(table.IsValid());
    EXPECT_EQ(table.NumRows(), 3);
}

TEST_F(VoTableTest, CorrectFieldCount) {
    Table table(ImagePath("ivoa_example.xml"));
    EXPECT_TRUE(table.IsValid());
    EXPECT_EQ(table.NumColumns(), 6);
}

TEST_F(VoTableTest, CorrectFieldNames) {
    Table table(ImagePath("ivoa_example.xml"));
    EXPECT_EQ(table[0]->name, "RA");
    EXPECT_EQ(table[1]->name, "Dec");
    EXPECT_EQ(table[2]->name, "Name");
    EXPECT_EQ(table[3]->name, "RVel");
    EXPECT_EQ(table[4]->name, "e_RVel");
    EXPECT_EQ(table[5]->name, "R");
}

TEST_F(VoTableTest, CorrectFieldUnits) {
    Table table(ImagePath("ivoa_example.xml"));
    EXPECT_EQ(table[0]->unit, "deg");
    EXPECT_EQ(table[1]->unit, "deg");
    EXPECT_TRUE(table[2]->unit.empty());
    EXPECT_EQ(table[3]->unit, "km/s");
    EXPECT_EQ(table[4]->unit, "km/s");
    EXPECT_EQ(table[5]->unit, "Mpc");
}

TEST_F(VoTableTest, CorrectFieldTypes) {
    Table table(ImagePath("ivoa_example.xml"));
    EXPECT_EQ(table[0]->data_type, CARTA::Float);
    EXPECT_EQ(table[1]->data_type, CARTA::Float);
    EXPECT_EQ(table[2]->data_type, CARTA::String);
    EXPECT_EQ(table[3]->data_type, CARTA::Int32);
    EXPECT_EQ(table[4]->data_type, CARTA::Int16);
    EXPECT_EQ(table[5]->data_type, CARTA::Float);
}

TEST_F(VoTableTest, CorrectFieldSizes) {
    Table table(ImagePath("ivoa_example.xml"));
    EXPECT_EQ(table[0]->data_type_size, 4);
    EXPECT_EQ(table[1]->data_type_size, 4);
    EXPECT_EQ(table[2]->data_type_size, 1);
    EXPECT_EQ(table[3]->data_type_size, 4);
    EXPECT_EQ(table[4]->data_type_size, 2);
    EXPECT_EQ(table[5]->data_type_size, 4);
}

TEST_F(VoTableTest, CorrectNameLookups) {
    Table table(ImagePath("ivoa_example.xml"));
    EXPECT_EQ(table["RA"]->name, "RA");
    EXPECT_EQ(table["Dec"]->name, "Dec");
    EXPECT_EQ(table["Name"]->name, "Name");
    EXPECT_EQ(table["RVel"]->name, "RVel");
    EXPECT_EQ(table["e_RVel"]->name, "e_RVel");
    EXPECT_EQ(table["R"]->name, "R");
    EXPECT_EQ(table["dummy"], nullptr);
    EXPECT_EQ(table[""], nullptr);
}

TEST_F(VoTableTest, CorrectIdLookups) {
    Table table(ImagePath("ivoa_example.xml"));
    EXPECT_EQ(table["col1"]->id, "col1");
    EXPECT_EQ(table["col2"]->id, "col2");
    EXPECT_EQ(table["col3"]->id, "col3");
    EXPECT_EQ(table["col4"]->id, "col4");
    EXPECT_EQ(table["col5"]->id, "col5");
    EXPECT_EQ(table["col6"]->id, "col6");
}

TEST_F(VoTableTest, CorrectColumnTypes) {
    Table table(ImagePath("ivoa_example.xml"));
    EXPECT_NE(DataColumn<float>::TryCast(table["col1"]), nullptr);
    EXPECT_EQ(DataColumn<double>::TryCast(table["col1"]), nullptr);

    EXPECT_NE(DataColumn<string>::TryCast(table["col3"]), nullptr);
    EXPECT_EQ(DataColumn<int>::TryCast(table["col3"]), nullptr);

    EXPECT_NE(DataColumn<int>::TryCast(table["col4"]), nullptr);
    EXPECT_EQ(DataColumn<string>::TryCast(table["col4"]), nullptr);

    EXPECT_NE(DataColumn<int16_t>::TryCast(table["col5"]), nullptr);
    EXPECT_EQ(DataColumn<int>::TryCast(table["col5"]), nullptr);
}

TEST_F(VoTableTest, CorrectDataValues) {
    Table table(ImagePath("ivoa_example.xml"));

    auto& col1_vals = DataColumn<float>::TryCast(table["col1"])->entries;
    EXPECT_EQ(col1_vals.size(), 3);
    EXPECT_FLOAT_EQ(col1_vals[0], 10.68f);
    EXPECT_FLOAT_EQ(col1_vals[1], 287.43f);

    auto& col2_vals = DataColumn<float>::TryCast(table["col2"])->entries;
    EXPECT_EQ(col2_vals.size(), 3);
    EXPECT_FLOAT_EQ(col2_vals[0], 41.27f);
    EXPECT_FLOAT_EQ(col2_vals[1], -63.85f);

    auto& col3_vals = DataColumn<string>::TryCast(table["col3"])->entries;
    EXPECT_EQ(col3_vals.size(), 3);
    EXPECT_EQ(col3_vals[0], "N 224");
    EXPECT_EQ(col3_vals[1], "N 6744");

    auto& col5_vals = DataColumn<int16_t>::TryCast(table["col5"])->entries;
    EXPECT_EQ(col5_vals.size(), 3);
    EXPECT_EQ(col5_vals[0], 5);
    EXPECT_EQ(col5_vals[1], 6);
}

TEST_F(VoTableTest, FailOnWrongFilterType) {
    Table table(ImagePath("ivoa_example.xml"));
    EXPECT_FALSE(table.View().StringFilter(table["dummy"], "N 224"));
    EXPECT_FALSE(table.View().StringFilter(table["col1"], "N 224"));

    EXPECT_FALSE(table.View().NumericFilter(table["dummy"], CARTA::RangeClosed, 0, 100));
    EXPECT_FALSE(table.View().NumericFilter(table["col3"], CARTA::RangeClosed, 0, 100));
}

TEST_F(VoTableTest, PassOnCorrectFilterType) {
    Table table(ImagePath("ivoa_example.xml"));
    EXPECT_TRUE(table.View().StringFilter(table["col3"], "N 224"));
    EXPECT_TRUE(table.View().NumericFilter(table["col1"], CARTA::RangeClosed, 0, 100));
}

TEST_F(VoTableTest, CaseSensitiveStringFilter) {
    Table table(ImagePath("ivoa_example.xml"));

    auto view = table.View();
    view.StringFilter(table["col3"], "N 224");
    EXPECT_EQ(view.NumRows(), 1);
    view.StringFilter(table["col3"], "n 224");
    EXPECT_EQ(view.NumRows(), 0);
    view.StringFilter(table["col3"], "N 598");
    EXPECT_EQ(view.NumRows(), 0);
}

TEST_F(VoTableTest, CaseInsensitiveStringFilter) {
    Table table(ImagePath("ivoa_example.xml"));

    auto view = table.View();
    view.StringFilter(table["col3"], "N 224", true);
    EXPECT_EQ(view.NumRows(), 1);
    view.StringFilter(table["col3"], "n 224", true);
    EXPECT_EQ(view.NumRows(), 1);
    view.StringFilter(table["col3"], "N 598", true);
    EXPECT_EQ(view.NumRows(), 0);
}

TEST_F(VoTableTest, FailFilterExtractMistypedValues) {
    Table table(ImagePath("ivoa_example.xml"));

    auto view = table.View();
    auto double_vals = view.Values<double>(table["col1"]);
    EXPECT_TRUE(double_vals.empty());
    auto string_vals = view.Values<string>(table["col1"]);
    EXPECT_TRUE(string_vals.empty());

    view.StringFilter(table["col3"], "N 6744");
    auto float_vals = view.Values<float>(table["col3"]);
    EXPECT_TRUE(float_vals.empty());
}

TEST_F(VoTableTest, FilterExtractValues) {
    Table table(ImagePath("ivoa_example.xml"));

    auto view = table.View();
    view.NumericFilter(table["col1"], CARTA::GreaterOrEqual, 10);
    auto string_vals = view.Values<string>(table["col3"]);
    EXPECT_EQ(string_vals.size(), 3);
    EXPECT_EQ(string_vals[0], "N 224");

    view.StringFilter(table["col3"], "N 6744");
    auto float_vals = view.Values<float>(table["col1"]);
    EXPECT_EQ(float_vals.size(), 1);
    EXPECT_FLOAT_EQ(float_vals[0], 287.43f);
}

TEST_F(VoTableTest, NumericFilterEqual) {
    Table table(ImagePath("ivoa_example.xml"));
    auto view = table.View();
    view.NumericFilter(table["RA"], CARTA::Equal, 287.43);
    EXPECT_EQ(view.NumRows(), 1);
    view.Reset();
    view.NumericFilter(table["e_RVel"], CARTA::Equal, 3);
    EXPECT_EQ(view.NumRows(), 1);
}

TEST_F(VoTableTest, NumericFilterNotEqual) {
    Table table(ImagePath("ivoa_example.xml"));
    auto view = table.View();
    view.NumericFilter(table["RA"], CARTA::NotEqual, 287.43);
    EXPECT_EQ(view.NumRows(), 2);
    view.Reset();
    view.NumericFilter(table["e_RVel"], CARTA::NotEqual, 3);
    EXPECT_EQ(view.NumRows(), 2);
}

TEST_F(VoTableTest, NumericFilterGreater) {
    Table table(ImagePath("ivoa_example.xml"));

    auto view = table.View();
    view.NumericFilter(table["col1"], CARTA::GreaterOrEqual, 10);
    EXPECT_EQ(view.NumRows(), 3);
    view.NumericFilter(table["col1"], CARTA::GreaterOrEqual, 11);
    EXPECT_EQ(view.NumRows(), 2);
    view.NumericFilter(table["col1"], CARTA::GreaterOrEqual, 300);
    EXPECT_EQ(view.NumRows(), 0);
}

TEST_F(VoTableTest, NumericFilterLesser) {
    Table table(ImagePath("ivoa_example.xml"));

    auto view = table.View();
    view.NumericFilter(table["col1"], CARTA::LessorOrEqual, 300);
    EXPECT_EQ(view.NumRows(), 3);
    view.NumericFilter(table["col1"], CARTA::LessorOrEqual, 11);
    EXPECT_EQ(view.NumRows(), 1);
    view.NumericFilter(table["col1"], CARTA::LessorOrEqual, 10);
    EXPECT_EQ(view.NumRows(), 0);
}

TEST_F(VoTableTest, NumericFilterRange) {
    Table table(ImagePath("ivoa_example.xml"));

    auto view = table.View();
    view.NumericFilter(table["col1"], CARTA::RangeClosed, 10, 300);
    EXPECT_EQ(view.NumRows(), 3);
    view.NumericFilter(table["col1"], CARTA::RangeClosed, 11, 300);
    EXPECT_EQ(view.NumRows(), 2);
    view.NumericFilter(table["col1"], CARTA::RangeClosed, 11, 14);
    EXPECT_EQ(view.NumRows(), 0);
}

TEST_F(VoTableTest, FailSortMissingColummn) {
    Table table(ImagePath("ivoa_example.xml"));

    auto view = table.View();
    EXPECT_FALSE(view.SortByColumn(nullptr));
}

TEST_F(VoTableTest, SortNumericAscending) {
    Table table(ImagePath("ivoa_example.xml"));

    auto view = table.View();
    EXPECT_TRUE(view.SortByColumn(table["col1"]));
    auto vals = view.Values<float>(table["col1"]);
    EXPECT_FLOAT_EQ(vals[0], 10.68f);
    EXPECT_FLOAT_EQ(vals[1], 23.48f);
    EXPECT_FLOAT_EQ(vals[2], 287.43f);
}

TEST_F(VoTableTest, SortNumericDescending) {
    Table table(ImagePath("ivoa_example.xml"));

    auto view = table.View();
    EXPECT_TRUE(view.SortByColumn(table["col1"], false));
    auto vals = view.Values<float>(table["col1"]);
    EXPECT_FLOAT_EQ(vals[0], 287.43f);
    EXPECT_FLOAT_EQ(vals[1], 23.48f);
    EXPECT_FLOAT_EQ(vals[2], 10.68f);
}

TEST_F(VoTableTest, SortNumericSubset) {
    Table table(ImagePath("ivoa_example.xml"));

    // Ascending sort
    auto view = table.View();
    view.NumericFilter(table["col1"], CARTA::RangeClosed, 11, 300);
    EXPECT_TRUE(view.SortByColumn(table["col1"]));
    auto vals = view.Values<float>(table["col1"]);
    EXPECT_FLOAT_EQ(vals[0], 23.48f);
    EXPECT_FLOAT_EQ(vals[1], 287.43f);
}

TEST_F(VoTableTest, SortStringAscending) {
    Table table(ImagePath("ivoa_example.xml"));

    auto view = table.View();
    EXPECT_TRUE(view.SortByColumn(table["col3"]));
    auto vals = view.Values<string>(table["col3"]);
    EXPECT_EQ(vals[0], "N 224");
    EXPECT_EQ(vals[1], "N 598");
    EXPECT_EQ(vals[2], "N 6744");
}

TEST_F(VoTableTest, SortStringDescending) {
    Table table(ImagePath("ivoa_example.xml"));

    auto view = table.View();
    EXPECT_TRUE(view.SortByColumn(table["col3"], false));
    auto vals = view.Values<string>(table["col3"]);
    EXPECT_EQ(vals[0], "N 6744");
    EXPECT_EQ(vals[1], "N 598");
    EXPECT_EQ(vals[2], "N 224");
}

TEST_F(VoTableTest, SortStringSubset) {
    Table table(ImagePath("ivoa_example.xml"));

    // Ascending sort
    auto view = table.View();
    view.NumericFilter(table["col1"], CARTA::RangeClosed, 11, 300);
    EXPECT_TRUE(view.SortByColumn(table["col3"]));
    auto vals = view.Values<string>(table["col3"]);
    EXPECT_EQ(vals[0], "N 598");
    EXPECT_EQ(vals[1], "N 6744");
}

TEST_F(VoTableTest, ParseArrayFile) {
    Table table(ImagePath("array_types.xml"));
    EXPECT_TRUE(table.IsValid());
    EXPECT_EQ(table.NumRows(), 3);
}

TEST_F(VoTableTest, IgnoreArrayTypes) {
    Table table(ImagePath("array_types.xml"));
    EXPECT_EQ(table["FixedArray"]->data_type, CARTA::UnsupportedType);
    EXPECT_EQ(table["BoundedArray"]->data_type, CARTA::UnsupportedType);
    EXPECT_EQ(table["UnboundedArray"]->data_type, CARTA::UnsupportedType);
    EXPECT_EQ(table["FixedArray2D"]->data_type, CARTA::UnsupportedType);
    EXPECT_EQ(table["BoundedArray2D"]->data_type, CARTA::UnsupportedType);
    EXPECT_EQ(table["UnboundedArray2D"]->data_type, CARTA::UnsupportedType);
}

TEST_F(VoTableTest, CorrectScalarData) {
    Table table(ImagePath("array_types.xml"));
    auto& scalar1_vals = DataColumn<float>::TryCast(table["Scalar1"])->entries;
    auto& scalar2_vals = DataColumn<float>::TryCast(table["Scalar2"])->entries;
    EXPECT_FLOAT_EQ(scalar1_vals[0], 1.0f);
    EXPECT_FLOAT_EQ(scalar1_vals[1], 2.0f);
    EXPECT_FLOAT_EQ(scalar1_vals[2], 3.0f);
    EXPECT_FLOAT_EQ(scalar2_vals[0], 2.0f);
    EXPECT_FLOAT_EQ(scalar2_vals[1], 4.0f);
    EXPECT_FLOAT_EQ(scalar2_vals[2], 6.0f);
}