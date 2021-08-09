/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND__UTIL_H_
#define CARTA_BACKEND__UTIL_H_

#include <cassert>
#include <filesystem>
#include <string>
#include <unordered_map>

#include <uWebSockets/HttpContext.h>

#include <casacore/images/Images/ImageInterface.h>
#include <casacore/images/Images/ImageOpener.h>
#include <casacore/scimath/Mathematics/GaussianBeam.h>

#include <carta-protobuf/region_stats.pb.h>
#include <carta-protobuf/spectral_profile.pb.h>

#include "Constants.h"
#include "ImageStats/BasicStatsCalculator.h"
#include "ImageStats/Histogram.h"

#ifdef _BOOST_FILESYSTEM_
#include <boost/filesystem.hpp>
namespace fs = boost::filesystem;
#else
#include <filesystem>
namespace fs = std::filesystem;
#endif

// Valid for little-endian only
#define FITS_MAGIC_NUMBER 0x504D4953
#define GZ_MAGIC_NUMBER 0x08088B1F
#define HDF5_MAGIC_NUMBER 0x46444889
#define XML_MAGIC_NUMBER 0x6D783F3C

// ************ Utilities *************
bool FindExecutablePath(std::string& path);
bool IsSubdirectory(std::string folder, std::string top_folder);
bool CheckFolderPaths(std::string& top_level_string, std::string& starting_string);
uint32_t GetMagicNumber(const std::string& filename);
bool IsCompressedFits(const std::string& filename);
int GetNumItems(const std::string& path);

fs::path SearchPath(std::string filename);

std::string GetGaussianInfo(const casacore::GaussianBeam& gaussian_beam);
std::string GetQuantityInfo(const casacore::Quantity& quantity);

// split input string into a vector of strings by delimiter
void SplitString(std::string& input, char delim, std::vector<std::string>& parts);

// Determine image type from filename
inline casacore::ImageOpener::ImageTypes CasacoreImageType(const std::string& filename) {
    return casacore::ImageOpener::imageType(filename);
}

// Image info: filename, type
casacore::String GetResolvedFilename(const std::string& root_dir, const std::string& directory, const std::string& file);
CARTA::FileType GetCartaFileType(const std::string& filename);

// stokes types and value conversion
int GetStokesValue(const CARTA::StokesType& stokes_type);
CARTA::StokesType GetStokesType(int stokes_value);

void GetSpectralCoordPreferences(
    casacore::ImageInterface<float>* image, bool& prefer_velocity, bool& optical_velocity, bool& prefer_wavelength, bool& air_wavelength);

// ************ Data Stream Helpers *************

void FillHistogramFromResults(CARTA::Histogram* histogram, const carta::BasicStats<float>& stats, const carta::Histogram& hist);

void FillSpectralProfileDataMessage(CARTA::SpectralProfileData& profile_message, std::string& coordinate,
    std::vector<CARTA::StatsType>& required_stats, std::map<CARTA::StatsType, std::vector<double>>& spectral_data);

void FillStatisticsValuesFromMap(CARTA::RegionStatsData& stats_data, const std::vector<CARTA::StatsType>& required_stats,
    std::map<CARTA::StatsType, double>& stats_value_map);

std::string IPAsText(std::string_view binary);

bool ValidateAuthToken(uWS::HttpRequest* http_request, const std::string& required_token);
bool ConstantTimeStringCompare(const std::string& a, const std::string& b);

// ************ Region Helpers *************

inline std::string RegionName(CARTA::RegionType type) {
    std::unordered_map<CARTA::RegionType, std::string> region_names = {{CARTA::RegionType::POINT, "point"},
        {CARTA::RegionType::LINE, "line"}, {CARTA::RegionType::POLYLINE, "polyline"}, {CARTA::RegionType::RECTANGLE, "rectangle"},
        {CARTA::RegionType::ELLIPSE, "ellipse"}, {CARTA::RegionType::ANNULUS, "annulus"}, {CARTA::RegionType::POLYGON, "polygon"}};
    return region_names[type];
}

// ************ structs *************
//
// Usage of the AxisRange:
//
// AxisRange() defines the full axis ALL_Z
// AxisRange(0) defines a single axis index, 0, in this example
// AxisRange(0, 1) defines the axis range including [0, 1] in this example
// AxisRange(0, 2) defines the axis range including [0, 1, 2] in this example
//
struct AxisRange {
    int from, to;
    AxisRange() {
        from = 0;
        to = ALL_Z;
    }
    AxisRange(int from_and_to_) {
        from = to = from_and_to_;
    }
    AxisRange(int from_, int to_) {
        from = from_;
        to = to_;
    }
};

struct PointXy {
    // Utilities for cursor and point regions
    float x, y;

    PointXy() {
        x = -1.0;
        y = -1.0;
    }
    PointXy(float x_, float y_) {
        x = x_;
        y = y_;
    }
    void operator=(const PointXy& other) {
        x = other.x;
        y = other.y;
    }
    bool operator==(const PointXy& rhs) const {
        if ((x != rhs.x) || (y != rhs.y)) {
            return false;
        }
        return true;
    }
    void ToIndex(int& x_index, int& y_index) {
        // convert float to int for index into image data array
        x_index = static_cast<int>(std::round(x));
        y_index = static_cast<int>(std::round(y));
    }
    bool InImage(int xrange, int yrange) {
        // returns whether x, y are within given image axis ranges
        int x_index, y_index;
        ToIndex(x_index, y_index);
        bool x_in_image = (x_index >= 0) && (x_index < xrange);
        bool y_in_image = (y_index >= 0) && (y_index < yrange);
        return (x_in_image && y_in_image);
    }
};

// Map for enmu CARTA:FileType to string
static std::unordered_map<CARTA::FileType, string> FileTypeString{{CARTA::FileType::CASA, "CASA"}, {CARTA::FileType::CRTF, "CRTF"},
    {CARTA::FileType::DS9_REG, "DS9"}, {CARTA::FileType::FITS, "FITS"}, {CARTA::FileType::HDF5, "HDF5"},
    {CARTA::FileType::MIRIAD, "MIRIAD"}, {CARTA::FileType::UNKNOWN, "Unknown"}};

#endif // CARTA_BACKEND__UTIL_H_
