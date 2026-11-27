/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>

#include "CommonTestUtilities.h"
#include "FileList/FileExtInfoLoader.h"
#include "FileList/FileInfoLoader.h"
#include "ImageData/Fits/CartaFitsImage.h"
#include "ImageData/Fits/CompressedFits.h"
#include "Session/Session.h"
#include "Table/Table.h"
#include "Util/Message.h"

using namespace carta;

class FileInfoLoaderTest : public ::testing::Test {
public:
    static CARTA::FileInfoRequest FileInfoRequest(const std::string& directory, const std::string& file, const std::string& hdu = "") {
        CARTA::FileInfoRequest file_info_request;
        file_info_request.set_directory(directory);
        file_info_request.set_file(file);
        file_info_request.set_hdu(hdu);
        return file_info_request;
    }

    static void CheckFileInfoLoader(
        const std::string& request_filename, const CARTA::FileType& request_file_type, const std::string& request_hdu = "") {
        std::string fullname = MixedImages() / request_filename;
        CARTA::FileInfo file_info;
        file_info.set_name(request_filename);

        FileInfoLoader info_loader = FileInfoLoader(fullname);
        bool file_info_ok = info_loader.FillFileInfo(file_info);
        EXPECT_TRUE(file_info_ok);

        CheckFileInfo(file_info, request_filename, request_file_type, request_hdu);
    }

    static void CheckFileInfo(const CARTA::FileInfo& file_info, const std::string& expect_filename, const CARTA::FileType& expect_file_type,
        const std::string& expect_hdu) {
        EXPECT_EQ(file_info.name(), expect_filename);
        EXPECT_EQ(file_info.type(), expect_file_type);
        EXPECT_EQ(file_info.hdu_list_size(), 1); // since sample files only has a HDU
        if (file_info.hdu_list_size() && !expect_hdu.empty()) {
            EXPECT_EQ(file_info.hdu_list(0), expect_hdu);
        }
    }
};

class FileExtInfoLoaderTest : public ::testing::Test {
public:
    static void CheckFileExtInfoLoader(
        const std::string& request_filename, const CARTA::FileType& request_file_type, const std::string& request_hdu = "") {
        std::string fullname = MixedImages() / request_filename;
        auto loader = carta::FileLoader::GetLoader(fullname);
        FileExtInfoLoader ext_info_loader(loader);
        bool file_info_ok;
        std::string message;
        std::map<std::string, CARTA::FileInfoExtended> file_info_extended_map;

        if (request_file_type == CARTA::FileType::FITS) {
            file_info_ok = ext_info_loader.FillFitsFileInfoMap(file_info_extended_map, fullname, message);
        } else {
            CARTA::FileInfoExtended file_info_ext;
            file_info_ok = ext_info_loader.FillFileExtInfo(file_info_ext, fullname, request_hdu, message);
            if (file_info_ok) {
                file_info_extended_map[request_hdu] = file_info_ext;
            }
        }

        CheckFileInfoExtended(file_info_extended_map, request_filename, request_hdu);
    }

    static void CheckFileInfoExtended(std::map<std::string, CARTA::FileInfoExtended> file_info_extended_map,
        const std::string& expect_filename, const std::string& expect_hdu) {
        EXPECT_NE(file_info_extended_map.find(expect_hdu), file_info_extended_map.end());
        if (file_info_extended_map.find(expect_hdu) == file_info_extended_map.end()) {
            return;
        }

        EXPECT_EQ(file_info_extended_map[expect_hdu].dimensions(), 4);
        EXPECT_EQ(file_info_extended_map[expect_hdu].width(), 6);
        EXPECT_EQ(file_info_extended_map[expect_hdu].height(), 6);
        EXPECT_EQ(file_info_extended_map[expect_hdu].depth(), 5);
        EXPECT_EQ(file_info_extended_map[expect_hdu].stokes(), 1);

        for (auto header_entry : file_info_extended_map[expect_hdu].header_entries()) {
            if (header_entry.name() == "SIMPLE") {
                CheckHeaderEntry(header_entry, "T", CARTA::EntryType::STRING, 0, "Standard FITS");
            } else if (header_entry.name() == "BITPIX") {
                CheckHeaderEntry(header_entry, "-32", CARTA::EntryType::INT, -32);
            } else if (header_entry.name() == "NAXIS") {
                CheckHeaderEntry(header_entry, "4", CARTA::EntryType::INT, 4);
            } else if (header_entry.name() == "NAXIS1") {
                CheckHeaderEntry(header_entry, "6", CARTA::EntryType::INT, 6);
            } else if (header_entry.name() == "NAXIS2") {
                CheckHeaderEntry(header_entry, "6", CARTA::EntryType::INT, 6);
            } else if (header_entry.name() == "NAXIS3") {
                CheckHeaderEntry(header_entry, "5", CARTA::EntryType::INT, 5);
            } else if (header_entry.name() == "NAXIS4") {
                CheckHeaderEntry(header_entry, "1", CARTA::EntryType::INT, 1);
            } else if (header_entry.name() == "EXTEND") {
                CheckHeaderEntry(header_entry, "T", CARTA::EntryType::STRING, 0);
            } else if (header_entry.name() == "BSCALE") {
                CheckHeaderEntry(header_entry, "1.000000000000E+00", CARTA::EntryType::FLOAT, 1, "PHYSICAL = PIXEL*BSCALE + BZERO");
            } else if (header_entry.name() == "PC1_1") {
                CheckHeaderEntry(header_entry, "1.000000000000E+00", CARTA::EntryType::FLOAT, 1);
            } else if (header_entry.name() == "PC2_2") {
                CheckHeaderEntry(header_entry, "1.000000000000E+00", CARTA::EntryType::FLOAT, 1);
            } else if (header_entry.name() == "PC3_3") {
                CheckHeaderEntry(header_entry, "1.000000000000E+00", CARTA::EntryType::FLOAT, 1);
            } else if (header_entry.name() == "PC4_4") {
                CheckHeaderEntry(header_entry, "1.000000000000E+00", CARTA::EntryType::FLOAT, 1);
            }
        }

        for (auto computed_entries : file_info_extended_map[expect_hdu].computed_entries()) {
            if (computed_entries.name() == "Name") {
                CheckHeaderEntry(computed_entries, expect_filename, CARTA::EntryType::STRING);
            } else if (computed_entries.name() == "HDU") {
                CheckHeaderEntry(computed_entries, "0", CARTA::EntryType::STRING);
            } else if (computed_entries.name() == "Shape") {
                CheckHeaderEntry(computed_entries, "[6, 6, 5, 1] (RA, DEC, FREQ, STOKES)", CARTA::EntryType::STRING);
            } else if (computed_entries.name() == "Number of channels") {
                CheckHeaderEntry(computed_entries, "5", CARTA::EntryType::INT, 5);
            } else if (computed_entries.name() == "Number of polarizations") {
                CheckHeaderEntry(computed_entries, "1", CARTA::EntryType::INT, 1);
            } else if (computed_entries.name() == "Coordinate type") {
                CheckHeaderEntry(computed_entries, "Right Ascension, Declination", CARTA::EntryType::STRING);
            } else if (computed_entries.name() == "Velocity definition") {
                CheckHeaderEntry(computed_entries, "RADIO", CARTA::EntryType::STRING);
            } else if (computed_entries.name() == "Pixel unit") {
                CheckHeaderEntry(computed_entries, "Jy/beam", CARTA::EntryType::STRING);
            } else if (computed_entries.name() == "Pixel increment") {
                CheckHeaderEntry(computed_entries, "-0.4\", 0.4\"", CARTA::EntryType::STRING);
            } else if (computed_entries.name() == "RA range") {
                CheckHeaderEntry(computed_entries, "[18:20:25.749, 18:20:25.888]", CARTA::EntryType::STRING);
            } else if (computed_entries.name() == "DEC range") {
                CheckHeaderEntry(computed_entries, "[-16.13.36.797, -16.13.34.797]", CARTA::EntryType::STRING);
            } else if (computed_entries.name() == "Frequency range") {
                CheckHeaderEntry(computed_entries, "[86.7504, 86.7494] (GHz)", CARTA::EntryType::STRING);
            } else if (computed_entries.name() == "Velocity range") {
                CheckHeaderEntry(computed_entries, "[13.3760, 16.7520] (km/s)", CARTA::EntryType::STRING);
            } else if (computed_entries.name() == "Stokes coverage") {
                CheckHeaderEntry(computed_entries, "[I]", CARTA::EntryType::STRING);
            }
        }
    }

    static void CheckHeaderEntry(const CARTA::HeaderEntry& header_entry, const std::string& expect_value,
        const CARTA::EntryType& expect_entry_type, double expect_numeric_value = std::numeric_limits<double>::quiet_NaN(),
        const std::string& expect_comment = "") {
        EXPECT_EQ(header_entry.value(), expect_value);
        EXPECT_EQ(header_entry.entry_type(), expect_entry_type);
        if (!isnan(expect_numeric_value)) {
            EXPECT_DOUBLE_EQ(header_entry.numeric_value(), expect_numeric_value);
        }
        if (!expect_comment.empty()) {
            EXPECT_EQ(header_entry.value(), expect_value);
        }
    }
};

class SessionFileInfoTest : public ::testing::Test {
public:
    static void CheckFileInfoResponse(const CARTA::FileInfoResponse& response, const std::string& expect_filename,
        const CARTA::FileType& expect_file_type, const std::string& expect_hdu) {
        EXPECT_TRUE(response.success());

        auto file_info = response.file_info();
        if (expect_file_type == CARTA::HDF5) {
            FileInfoLoaderTest::CheckFileInfo(file_info, expect_filename, expect_file_type, expect_hdu);
        } else {
            FileInfoLoaderTest::CheckFileInfo(file_info, expect_filename, expect_file_type, "");
        }

        auto file_info_extended = response.file_info_extended();
        // convert protobuf map to std map
        std::map<std::string, CARTA::FileInfoExtended> file_info_extended_map = {file_info_extended.begin(), file_info_extended.end()};

        FileExtInfoLoaderTest::CheckFileInfoExtended(file_info_extended_map, expect_filename, expect_hdu);
    }
};

// Writes HDUs (header cards, data) to a gzip FITS file, padding each part to 2880 bytes
static void WriteGzFits(const fs::path& path, const std::vector<std::pair<std::vector<std::string>, std::string>>& hdus) {
    std::string fits;
    auto pad = [&](char c) { fits.append((2880 - fits.size() % 2880) % 2880, c); };
    for (const auto& [cards, data] : hdus) {
        for (const auto& card : cards) {
            fits += card + std::string(80 - card.size(), ' ');
        }
        fits += "END" + std::string(77, ' ');
        pad(' ');
        fits += data;
        pad('\0');
    }
    gzFile file = gzopen(path.c_str(), "wb");
    gzwrite(file, fits.data(), fits.size());
    gzclose(file);
}

static std::string Card(std::string keyword, const std::string& value) {
    keyword.resize(8, ' ');
    return keyword + "= " + value;
}

class TestSession : public Session {
public:
    TestSession() : Session(nullptr, nullptr, 0, "", nullptr) {}

    void TestFileInfo(const std::string& request_filename, const CARTA::FileType& request_file_type, const std::string& request_hdu = "") {
        auto request = FileInfoLoaderTest::FileInfoRequest(MixedImages(), request_filename, request_hdu);
        CARTA::FileInfoResponse response;
        auto& file_info = *response.mutable_file_info();
        std::map<std::string, CARTA::FileInfoExtended> extended_info_map;
        bool support_aips_beam(false);
        string message;
        bool success = FillExtendedFileInfo(
            extended_info_map, file_info, request.directory(), request.file(), request.hdu(), support_aips_beam, message);
        if (success) {
            *response.mutable_file_info_extended() = {extended_info_map.begin(), extended_info_map.end()};
        }
        response.set_success(success);

        SessionFileInfoTest::CheckFileInfoResponse(response, request_filename, request_file_type, request_hdu);
    }

    void TestGzFitsShape(const std::vector<std::string>& cards, const std::string& expect_shape) {
        auto folder = TestRoot() / "data" / "generated";
        std::string filename = "shape_test.fits.gz";
        WriteGzFits(folder / filename, {{cards, std::string(16, '\0')}});
        std::map<std::string, CARTA::FileInfoExtended> extended_info_map;
        CARTA::FileInfo file_info;
        bool support_aips_beam(false);
        string message;
        bool success = FillExtendedFileInfo(extended_info_map, file_info, folder, filename, "", support_aips_beam, message);
        EXPECT_TRUE(success);
        ASSERT_EQ(extended_info_map.size(), 1);

        for (auto computed_entries : extended_info_map.begin()->second.computed_entries()) {
            if (computed_entries.name() == "Shape") {
                FileExtInfoLoaderTest::CheckHeaderEntry(computed_entries, expect_shape, CARTA::EntryType::STRING);
            }
        }
    }

    FRIEND_TEST(FileExtInfoLoaderTest, FitsHistoryEntries);
    FRIEND_TEST(FileExtInfoLoaderTest, FitsPathIsNotCfitsioSyntax);
};

TEST_F(FileInfoLoaderTest, CasaFile) {
    CheckFileInfoLoader("M17_SWex_unit.image", CARTA::FileType::CASA);
}

TEST_F(FileInfoLoaderTest, FitsFile) {
    CheckFileInfoLoader("M17_SWex_unit.fits", CARTA::FileType::FITS);
}

TEST_F(FileInfoLoaderTest, Hdf5File) {
    CheckFileInfoLoader("M17_SWex_unit.hdf5", CARTA::FileType::HDF5, "0"); // FileInfoLoader only get HDU list for a HDF5 file
}

TEST_F(FileInfoLoaderTest, MiriadFile) {
    CheckFileInfoLoader("M17_SWex_unit.miriad", CARTA::FileType::MIRIAD);
}

TEST_F(FileExtInfoLoaderTest, CasaFile) {
    CheckFileExtInfoLoader("M17_SWex_unit.image", CARTA::FileType::CASA);
}

TEST_F(FileExtInfoLoaderTest, FitsFile) {
    CheckFileExtInfoLoader("M17_SWex_unit.fits", CARTA::FileType::FITS, "0");
}

TEST_F(FileExtInfoLoaderTest, Hdf5File) {
    CheckFileExtInfoLoader("M17_SWex_unit.hdf5", CARTA::FileType::HDF5, "0");
}

TEST_F(FileExtInfoLoaderTest, MiriadFile) {
    CheckFileExtInfoLoader("M17_SWex_unit.miriad", CARTA::FileType::MIRIAD);
}

TEST_F(SessionFileInfoTest, CasaFile) {
    TestSession t_session;
    t_session.TestFileInfo("M17_SWex_unit.image", CARTA::FileType::CASA);
}

TEST_F(FileExtInfoLoaderTest, FitsHistoryEntries) {
    TestSession t_session;
    string message;
    std::string full_name;
    std::string hdu;
    CARTA::FileInfoExtended extended_info;
    CARTA::FileInfo file_info;
    bool support_aips_beam(false);
    bool success = t_session.FillExtendedFileInfo(
        extended_info, file_info, FitsImages(), "noise_10px_10px.fits", hdu, support_aips_beam, message, full_name);
    EXPECT_EQ(success, true);

    int num_history_entries = 0;
    for (auto entry : extended_info.header_entries()) {
        if (entry.name().find("HISTORY") != std::string::npos) {
            num_history_entries++;
        }
    }
    EXPECT_EQ(num_history_entries, 15);
}

TEST_F(FileExtInfoLoaderTest, GzFitsAxisKeywordsBeyondNaxis) {
    TestSession t_session;
    t_session.TestGzFitsShape({Card("SIMPLE", "T"), Card("BITPIX", "-32"), Card("NAXIS", "2"), Card("NAXIS1", "2"), Card("NAXIS2", "2"),
                                  Card("NAXIS5", "1094795585"), Card("NAXIS1", "2")},
        "[2, 2] (NA, NA)");
}

TEST_F(FileExtInfoLoaderTest, GzFitsAlternateWcsAxisNames) {
    TestSession t_session;
    t_session.TestGzFitsShape(
        {Card("SIMPLE", "T"), Card("BITPIX", "-32"), Card("NAXIS", "2"), Card("NAXIS1", "2"), Card("NAXIS2", "2"),
            Card("CTYPE1", "'RA---SIN'"), Card("CTYPE2", "'DEC--SIN'"), Card("CTYPE1A", "'GLAT-SIN'"), Card("CTYPE2A", "'GLON-SIN'")},
        "[2, 2] (RA, DEC)");
}

TEST_F(FileExtInfoLoaderTest, GzFitsBeamTableColumnTen) {
    auto path = TestRoot() / "data" / "generated" / "beam_column_test.fits.gz";
    std::vector<std::string> image = {
        Card("SIMPLE", "T"), Card("BITPIX", "-32"), Card("NAXIS", "2"), Card("NAXIS1", "2"), Card("NAXIS2", "2")};
    std::vector<std::string> table = {Card("XTENSION", "'BINTABLE'"), Card("BITPIX", "8"), Card("NAXIS", "2"), Card("NAXIS1", "40"),
        Card("NAXIS2", "1"), Card("PCOUNT", "0"), Card("GCOUNT", "1"), Card("TFIELDS", "10")};
    std::vector<std::string> names = {"BMIN", "BPA", "CHAN", "POL", "X5", "X6", "X7", "X8", "X9", "BMAJ"};
    for (int i = 0; i < names.size(); ++i) {
        auto n = std::to_string(i + 1);
        table.push_back(Card("TTYPE" + n, "'" + names[i] + "'"));
        table.push_back(Card("TFORM" + n, (names[i] == "CHAN" || names[i] == "POL") ? "'1J'" : "'1E'"));
    }
    for (std::string n : {"1", "2", "10"}) {
        table.push_back(Card("TUNIT" + n, n == "2" ? "'deg'" : "'arcsec'"));
    }
    for (const auto& card : {Card("EXTNAME", "'BEAMS'"), Card("NCHAN", "1"), Card("NPOL", "1")}) {
        table.push_back(card);
    }
    // BMIN 2", BPA 30 deg, BMAJ 3" in column 10
    std::string row;
    for (float value : {2.0f, 30.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 3.0f}) {
        uint32_t bits;
        memcpy(&bits, &value, 4);
        for (int shift = 24; shift >= 0; shift -= 8) {
            row += static_cast<char>((bits >> shift) & 0xff);
        }
    }
    WriteGzFits(path, {{image, std::string(16, '\0')}, {table, row}});

    CompressedFits fits(path.string());
    std::map<std::string, CARTA::FileInfoExtended> hdu_info_map;
    fits.GetFitsHeaderInfo(hdu_info_map);
    bool is_history_beam(false);
    auto beam_set = fits.GetBeamSet(is_history_beam);
    ASSERT_EQ(beam_set.size(), 1);
    EXPECT_FLOAT_EQ(beam_set.getBeam(0, 0).getMajor("arcsec"), 3.0);
    EXPECT_FLOAT_EQ(beam_set.getBeam(0, 0).getMinor("arcsec"), 2.0);
}

TEST_F(FileExtInfoLoaderTest, FitsPathIsNotCfitsioSyntax) {
    // cfitsio would read <tmp>/a.fits(<tmp>/out/<name>) as "copy <tmp>/a.fits to <tmp>/out/<name>"
    auto tmp = fs::canonical(fs::temp_directory_path()) / "carta-cfitsio-syntax-test";
    fs::remove_all(tmp);
    fs::create_directories(tmp / "out");
    fs::copy_file(FitsImages() / "noise_10px_10px.fits", tmp / "a.fits");
    auto make_leaf = [&](const std::string& name, const std::string& image = "noise_10px_10px.fits") {
        fs::path leaf = tmp.string() + "/a.fits(" + tmp.string() + "/out/" + name + ")";
        fs::create_directories(leaf.parent_path());
        fs::copy_file(FitsImages() / image, leaf);
        return leaf;
    };

    auto leaf = make_leaf("info.fits");
    TestSession t_session;
    std::map<std::string, CARTA::FileInfoExtended> extended_info_map;
    CARTA::FileInfo file_info;
    std::string message;
    t_session.FillExtendedFileInfo(extended_info_map, file_info, leaf.parent_path().string(), leaf.filename().string(), "", false, message);
    EXPECT_FALSE(fs::exists(tmp / "out/info.fits"));

    leaf = make_leaf("image.fits", "small_perplanebeam.fits"); // also reads the BEAMS table
    try {
        CartaFitsImage image(leaf.string(), 0);
    } catch (const casacore::AipsError& err) {
    }
    EXPECT_FALSE(fs::exists(tmp / "out/image.fits"));

    leaf = make_leaf("catalog.fits");
    carta::Table table(leaf.string());
    EXPECT_FALSE(fs::exists(tmp / "out/catalog.fits"));
    fs::remove_all(tmp);
}

TEST_F(FileExtInfoLoaderTest, FitsSaveNameIsNotCfitsioSyntax) {
    // cfitsio would read x.fits(a.fits) as "create x.fits from the template a.fits"
    auto tmp = fs::canonical(fs::temp_directory_path()) / "carta-cfitsio-save-test";
    fs::remove_all(tmp);
    fs::create_directories(tmp);
    fs::copy_file(FitsImages() / "noise_10px_10px.fits", tmp / "a.fits");
    Frame frame(0, FileLoader::GetLoader((tmp / "a.fits").string()), "0");

    auto save = [&](const std::string& name) {
        CARTA::SaveFile save_file;
        save_file.set_output_file_directory(tmp.string());
        save_file.set_output_file_name(name);
        save_file.set_output_file_type(CARTA::FileType::FITS);
        save_file.set_rest_freq(NAN);
        CARTA::SaveFileAck ack;
        frame.SaveFile("/", save_file, ack, nullptr);
        return ack.success();
    };

    EXPECT_FALSE(save("x.fits(" + (tmp / "a.fits").string() + ")"));
    EXPECT_FALSE(save("x.fits[compress]"));
    EXPECT_FALSE(fs::exists(tmp / "x.fits"));
    EXPECT_TRUE(save("x.fits"));
    EXPECT_TRUE(fs::exists(tmp / "x.fits"));
    fs::remove_all(tmp);
}

TEST_F(SessionFileInfoTest, FitsFile) {
    TestSession t_session;
    t_session.TestFileInfo("M17_SWex_unit.fits", CARTA::FileType::FITS, "0");
}

TEST_F(SessionFileInfoTest, Hdf5File) {
    TestSession t_session;
    t_session.TestFileInfo("M17_SWex_unit.hdf5", CARTA::FileType::HDF5, "0");
}

TEST_F(SessionFileInfoTest, MiriadFile) {
    TestSession t_session;
    t_session.TestFileInfo("M17_SWex_unit.miriad", CARTA::FileType::MIRIAD);
}

class FileExtInfoTestWithParam : public testing::TestWithParam<std::string> {};

TEST_P(FileExtInfoTestWithParam, Issue1632) {
    auto file_name = GetParam();
    auto path = Issues("1632") / file_name;
    auto loader = carta::FileLoader::GetLoader(path);
    FileExtInfoLoader ext_info_loader(loader);
    bool file_info_ok;
    std::string message;
    std::map<std::string, CARTA::FileInfoExtended> file_info_extended_map;
    CARTA::FileInfoExtended file_info_ext;

    if (file_name.substr(file_name.size() - 3, 3) == ".gz") {
        file_info_ok = ext_info_loader.FillFitsFileInfoMap(file_info_extended_map, path, message);
        if (file_info_ok) {
            file_info_ext = file_info_extended_map[""];
        }
    } else {
        file_info_ok = ext_info_loader.FillFileExtInfo(file_info_ext, path, "", message);
    }

    for (auto header_entry : file_info_ext.header_entries()) {
        if (header_entry.name() == "CDELT1" || header_entry.name() == "CDELT2") {
            EXPECT_DOUBLE_EQ(header_entry.numeric_value(), 5E-5);
        }
    }
}

INSTANTIATE_TEST_SUITE_P(Issue1632Files, FileExtInfoTestWithParam, ::testing::Values("bad.fits", "bad.fits.gz"));
