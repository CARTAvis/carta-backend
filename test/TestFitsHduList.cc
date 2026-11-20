/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <fitsio.h>
#include <gtest/gtest.h>
#include <zlib.h>
#include <fstream>

#include "CommonTestUtilities.h"
#include "FileList/FitsHduList.h"
#include "Util/FileSystem.h"

using namespace carta;

namespace {

std::string TempPath(const std::string& name) {
    return (fs::temp_directory_path() / name).string();
}

// A .fits.fz has an empty primary HDU and the image in a tile-compressed extension
std::string WriteCompressedFits() {
    auto path = TempPath("carta_test_tile_compressed.fits.fz");
    std::error_code error_code;
    fs::remove(path, error_code);

    fitsfile* fptr(nullptr);
    int status(0);
    fits_create_file(&fptr, path.c_str(), &status);
    fits_set_compression_type(fptr, RICE_1, &status);
    long naxes[2] = {10, 10};
    fits_create_img(fptr, FLOAT_IMG, 2, naxes, &status);
    std::vector<float> data(100, 1.0F);
    fits_write_img(fptr, TFLOAT, 1, data.size(), data.data(), &status);
    fits_close_file(fptr, &status);
    return status ? "" : path;
}

std::string WriteGzippedFits(const std::string& source) {
    auto path = TempPath("carta_test_gzipped.fits.gz");
    std::ifstream input(source, std::ios::binary);
    std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());

    auto* gz_file = gzopen(path.c_str(), "wb");
    if (!gz_file) {
        return "";
    }
    gzwrite(gz_file, contents.data(), contents.size());
    gzclose(gz_file);
    return path;
}

} // namespace

TEST(FitsHduListFirstImage, PlainFitsUsesPrimaryHdu) {
    std::string hdu, error;
    EXPECT_TRUE(FitsHduList::GetFirstImageHdu((FitsImages() / "noise_10px_10px.fits").string(), hdu, error));
    EXPECT_EQ(hdu, "0");
}

TEST(FitsHduListFirstImage, TileCompressedFitsUsesImageExtension) {
    auto path = WriteCompressedFits();
    ASSERT_FALSE(path.empty());
    std::string hdu, error;
    EXPECT_TRUE(FitsHduList::GetFirstImageHdu(path, hdu, error));
    // The extension name must be stripped: casacore parses the HDU string strictly
    EXPECT_EQ(hdu, "1");
    std::error_code error_code;
    fs::remove(path, error_code);
}

TEST(FitsHduListFirstImage, GzippedFitsIsRead) {
    auto path = WriteGzippedFits((FitsImages() / "noise_10px_10px.fits").string());
    ASSERT_FALSE(path.empty());
    std::string hdu, error;
    EXPECT_TRUE(FitsHduList::GetFirstImageHdu(path, hdu, error));
    EXPECT_EQ(hdu, "0");
    std::error_code error_code;
    fs::remove(path, error_code);
}

TEST(FitsHduListFirstImage, MissingFileReportsError) {
    std::string hdu, error;
    EXPECT_FALSE(FitsHduList::GetFirstImageHdu("/nonexistent/file.fits", hdu, error));
    EXPECT_FALSE(error.empty());
}
