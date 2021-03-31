/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "Util.h"

#include <climits>
#include <cmath>
#include <fstream>
#include <regex>

#include <casacore/casa/OS/File.h>

#include "ImageData/CartaMiriadImage.h"
#include "Logger/Logger.h"

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

using namespace std;

bool CheckFolderPaths(string& top_level_string, string& starting_string) {
    // TODO: is this code needed at all? Was it a weird workaround?
    {
        if (top_level_string == "base" && starting_string == "root") {
            spdlog::critical("Must set top level or starting directory. Exiting carta.");
            return false;
        }
        if (top_level_string == "base")
            top_level_string = starting_string;
        if (starting_string == "root")
            starting_string = top_level_string;
    }
    // TODO: Migrate to std::filesystem
    // check top level
    casacore::File top_level_folder(top_level_string);
    if (!(top_level_folder.exists() && top_level_folder.isDirectory(true) && top_level_folder.isReadable() &&
            top_level_folder.isExecutable())) {
        spdlog::critical("Invalid top level directory, does not exist or is not a readable directory. Exiting carta.");
        return false;
    }
    // absolute path: resolve symlinks, relative paths, env vars e.g. $HOME
    try {
        top_level_string = top_level_folder.path().resolvedName(); // fails on top level folder /
    } catch (casacore::AipsError& err) {
        try {
            top_level_string = top_level_folder.path().absoluteName();
        } catch (casacore::AipsError& err) {
            spdlog::error(err.getMesg());
        }
        if (top_level_string.empty())
            top_level_string = "/";
    }
    // check starting folder
    casacore::File starting_folder(starting_string);
    if (!(starting_folder.exists() && starting_folder.isDirectory(true) && starting_folder.isReadable() &&
            starting_folder.isExecutable())) {
        spdlog::warn("Invalid starting directory, using the provided top level directory instead.");
        starting_string = top_level_string;
    } else {
        // absolute path: resolve symlinks, relative paths, env vars e.g. $HOME
        try {
            starting_string = starting_folder.path().resolvedName(); // fails on top level folder /
        } catch (casacore::AipsError& err) {
            try {
                starting_string = starting_folder.path().absoluteName();
            } catch (casacore::AipsError& err) {
                spdlog::error(err.getMesg());
            }
            if (starting_string.empty())
                starting_string = "/";
        }
    }
    bool is_subdirectory = IsSubdirectory(starting_string, top_level_string);
    if (!is_subdirectory) {
        spdlog::critical("Starting {} must be a subdirectory of top level {}. Exiting carta.", starting_string, top_level_string);
        return false;
    }
    return true;
}

bool IsSubdirectory(string folder, string top_folder) {
    folder = casacore::Path(folder).absoluteName();
    top_folder = casacore::Path(top_folder).absoluteName();
    if (top_folder.empty()) {
        return true;
    }
    if (folder == top_folder) {
        return true;
    }
    casacore::Path folder_path(folder);
    string parent_string(folder_path.dirName());
    if (parent_string == top_folder) {
        return true;
    }
    while (parent_string != top_folder) { // navigate up directory tree
        folder_path = casacore::Path(parent_string);
        parent_string = folder_path.dirName();
        if (parent_string == top_folder) {
            return true;
        } else if (parent_string == "/") {
            break;
        }
    }
    return false;
}

uint32_t GetMagicNumber(const string& filename) {
    uint32_t magic_number = 0;

    ifstream input_file(filename);
    if (input_file) {
        input_file.read((char*)&magic_number, sizeof(magic_number));
        input_file.close();
    }
    return magic_number;
}

void SplitString(string& input, char delim, vector<string>& parts) {
    // util to split input string into parts by delimiter
    parts.clear();
    stringstream ss(input);
    string item;
    while (getline(ss, item, delim)) {
        if (!item.empty()) {
            if (item.back() == '\r') {
                item.pop_back();
            }
            parts.push_back(item);
        }
    }
}

casacore::String GetResolvedFilename(const string& root_dir, const string& directory, const string& file) {
    // Given directory (relative to root folder) and file, return resolved path and filename
    // (absolute pathname with symlinks resolved)
    casacore::String resolved_filename;
    casacore::Path root_path(root_dir);
    root_path.append(directory);
    root_path.append(file);
    casacore::File cc_file(root_path);
    if (cc_file.exists()) {
        try {
            resolved_filename = cc_file.path().resolvedName();
        } catch (casacore::AipsError& err) {
            // return empty string
        }
    }
    return resolved_filename;
}

CARTA::FileType GetCartaFileType(const string& filename) {
    // get casacore image type then convert to carta file type
    switch (CasacoreImageType(filename)) {
        case casacore::ImageOpener::AIPSPP:
        case casacore::ImageOpener::IMAGECONCAT:
        case casacore::ImageOpener::IMAGEEXPR:
        case casacore::ImageOpener::COMPLISTIMAGE:
            return CARTA::FileType::CASA;
        case casacore::ImageOpener::FITS:
            return CARTA::FileType::FITS;
        case casacore::ImageOpener::MIRIAD:
            return CARTA::FileType::MIRIAD;
        case casacore::ImageOpener::HDF5:
            return CARTA::FileType::HDF5;
        case casacore::ImageOpener::GIPSY:
        case casacore::ImageOpener::CAIPS:
        case casacore::ImageOpener::NEWSTAR:
        default:
            return CARTA::FileType::UNKNOWN;
    }
}

void GetSpectralCoordPreferences(
    casacore::ImageInterface<float>* image, bool& prefer_velocity, bool& optical_velocity, bool& prefer_wavelength, bool& air_wavelength) {
    prefer_velocity = optical_velocity = prefer_wavelength = air_wavelength = false;
    casacore::CoordinateSystem coord_sys(image->coordinates());
    if (coord_sys.hasSpectralAxis()) { // prefer spectral axis native type
        casacore::SpectralCoordinate::SpecType native_type;
        if (image->imageType() == "CartaMiriadImage") { // workaround to get correct native type
            carta::CartaMiriadImage* miriad_image = static_cast<carta::CartaMiriadImage*>(image);
            native_type = miriad_image->NativeType();
        } else {
            native_type = coord_sys.spectralCoordinate().nativeType();
        }
        switch (native_type) {
            case casacore::SpectralCoordinate::FREQ: {
                break;
            }
            case casacore::SpectralCoordinate::VRAD:
            case casacore::SpectralCoordinate::BETA: {
                prefer_velocity = true;
                break;
            }
            case casacore::SpectralCoordinate::VOPT: {
                prefer_velocity = true;

                // Check doppler type; oddly, native type can be VOPT but doppler is RADIO--?
                casacore::MDoppler::Types vel_doppler(coord_sys.spectralCoordinate().velocityDoppler());
                if ((vel_doppler == casacore::MDoppler::Z) || (vel_doppler == casacore::MDoppler::OPTICAL)) {
                    optical_velocity = true;
                }
                break;
            }
            case casacore::SpectralCoordinate::WAVE: {
                prefer_wavelength = true;
                break;
            }
            case casacore::SpectralCoordinate::AWAV: {
                prefer_wavelength = true;
                air_wavelength = true;
                break;
            }
        }
    }
}

void FillHistogramFromResults(CARTA::Histogram* histogram, const carta::BasicStats<float>& stats, const carta::Histogram& hist) {
    if (histogram == nullptr) {
        return;
    }
    histogram->set_num_bins(hist.GetNbins());
    histogram->set_bin_width(hist.GetBinWidth());
    histogram->set_first_bin_center(hist.GetBinCenter());
    *histogram->mutable_bins() = {hist.GetHistogramBins().begin(), hist.GetHistogramBins().end()};
    histogram->set_mean(stats.mean);
    histogram->set_std_dev(stats.stdDev);
}

void FillSpectralProfileDataMessage(CARTA::SpectralProfileData& profile_message, string& coordinate,
    vector<CARTA::StatsType>& required_stats, map<CARTA::StatsType, vector<double>>& spectral_data) {
    for (auto stats_type : required_stats) {
        // one SpectralProfile per stats type
        auto new_profile = profile_message.add_profiles();
        new_profile->set_coordinate(coordinate);
        new_profile->set_stats_type(stats_type);

        if (spectral_data.find(stats_type) == spectral_data.end()) { // stat not provided
            double nan_value = nan("");
            new_profile->set_raw_values_fp64(&nan_value, sizeof(double));
        } else {
            new_profile->set_raw_values_fp64(spectral_data[stats_type].data(), spectral_data[stats_type].size() * sizeof(double));
        }
    }
}

void FillStatisticsValuesFromMap(
    CARTA::RegionStatsData& stats_data, vector<CARTA::StatsType>& required_stats, map<CARTA::StatsType, double>& stats_value_map) {
    // inserts values from map into message StatisticsValue field; needed by Frame and RegionDataHandler
    for (auto type : required_stats) {
        double value(0.0); // default
        auto carta_stats_type = static_cast<CARTA::StatsType>(type);
        if (stats_value_map.find(carta_stats_type) != stats_value_map.end()) { // stat found
            value = stats_value_map[carta_stats_type];
        } else { // stat not provided
            if (carta_stats_type != CARTA::StatsType::NumPixels) {
                value = nan("");
            }
        }

        // add StatisticsValue to message
        auto stats_value = stats_data.add_statistics();
        stats_value->set_stats_type(carta_stats_type);
        stats_value->set_value(value);
    }
}

void ConvertCoordinateToAxes(const string& coordinate, int& axis_index, int& stokes_index) {
    // converts profile string into axis, stokes index into image shape
    // axis
    char axis_char(coordinate.back());
    if (axis_char == 'x') {
        axis_index = 0;
    } else if (axis_char == 'y') {
        axis_index = 1;
    } else if (axis_char == 'z') {
        axis_index = -1; // not used
    }

    // stokes
    if (coordinate.size() == 2) {
        char stokes_char(coordinate.front());
        if (stokes_char == 'I') {
            stokes_index = 0;
        } else if (stokes_char == 'Q') {
            stokes_index = 1;
        } else if (stokes_char == 'U') {
            stokes_index = 2;
        } else if (stokes_char == 'V') {
            stokes_index = 3;
        }
    } else {
        stokes_index = -1;
    }
}

string IPAsText(string_view binary) {
    string result;
    if (!binary.length()) {
        return result;
    }

    unsigned char* b = (unsigned char*)binary.data();
    if (binary.length() == 4) {
        result = fmt::format("{0:d}.{1:d}.{2:d}.{3:d}", b[0], b[1], b[2], b[3]);
    } else {
        result = fmt::format("::{0:x}{1:x}:{2:d}.{3:d}.{4:d}.{5:d}", b[10], b[11], b[12], b[13], b[14], b[15]);
    }

    return result;
}

string GetAuthToken(uWS::HttpRequest* http_request) {
    string req_token;
    // First try the cookie auth token
    string cookie_header = string(http_request->getHeader("cookie"));
    if (!cookie_header.empty()) {
        regex header_regex("carta-auth-token=(.+?)(?:;|$)");
        smatch sm;
        if (regex_search(cookie_header, sm, header_regex) && sm.size() == 2) {
            return sm[1];
        }
    }

    // Try the standard authorization bearer token approach
    string auth_header = string(http_request->getHeader("authorization"));
    regex auth_regex(R"(^Bearer\s+(\S+)$)");
    smatch sm;
    if (regex_search(auth_header, sm, auth_regex) && sm.size() == 2) {
        return sm[1].str();
    }

    // Finally, fall back to the non-standard auth token header
    // TODO: Should this be supported any more? Where is it used?
    return string(http_request->getHeader("carta-auth-token"));
}

bool FindExecutablePath(string& path) {
    char path_buffer[PATH_MAX + 1];
#ifdef __APPLE__
    uint32_t len = sizeof(path_buffer);

    if (_NSGetExecutablePath(path_buffer, &len) != 0) {
        return false;
    }
#else
    const int len = int(readlink("/proc/self/exe", path_buffer, PATH_MAX));

    if (len == -1) {
        return false;
    }

    path_buffer[len] = 0;
#endif
    path = path_buffer;
    return true;
}
