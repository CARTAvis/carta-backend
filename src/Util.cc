/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "Util.h"
#include "Logger/Logger.h"

using namespace std;

bool CheckRootBaseFolders(string& root, string& base) {
    if (root == "base" && base == "root") {
        spdlog::critical("Must set root or base directory. Exiting carta.");
        return false;
    }
    if (root == "base")
        root = base;
    if (base == "root")
        base = root;

    // check root
    casacore::File root_folder(root);
    if (!(root_folder.exists() && root_folder.isDirectory(true) && root_folder.isReadable() && root_folder.isExecutable())) {
        spdlog::critical("Invalid root directory, does not exist or is not a readable directory. Exiting carta.");
        return false;
    }
    // absolute path: resolve symlinks, relative paths, env vars e.g. $HOME
    try {
        root = root_folder.path().resolvedName(); // fails on root folder /
    } catch (casacore::AipsError& err) {
        try {
            root = root_folder.path().absoluteName();
        } catch (casacore::AipsError& err) {
            spdlog::error(err.getMesg());
        }
        if (root.empty())
            root = "/";
    }
    // check base
    casacore::File base_folder(base);
    if (!(base_folder.exists() && base_folder.isDirectory(true) && base_folder.isReadable() && base_folder.isExecutable())) {
        spdlog::warn("Invalid base directory, using the provided root directory instead.");
        base = root;
    } else {
        // absolute path: resolve symlinks, relative paths, env vars e.g. $HOME
        try {
            base = base_folder.path().resolvedName(); // fails on root folder /
        } catch (casacore::AipsError& err) {
            try {
                base = base_folder.path().absoluteName();
            } catch (casacore::AipsError& err) {
                spdlog::error(err.getMesg());
            }
            if (base.empty())
                base = "/";
        }
    }
    // check if base is same as or subdir of root
    if (base != root) {
        bool is_subdirectory(false);
        casacore::Path base_path(base);
        casacore::String parent_string(base_path.dirName()), root_string(root);
        if (parent_string == root_string)
            is_subdirectory = true;
        while (!is_subdirectory && (parent_string != root_string)) { // navigate up directory tree
            base_path = casacore::Path(parent_string);
            parent_string = base_path.dirName();
            if (parent_string == root_string) {
                is_subdirectory = true;
            } else if (parent_string == "/") {
                break;
            }
        }
        if (!is_subdirectory) {
            spdlog::critical("Base {} must be a subdirectory of root {}. Exiting carta.", base, root);
            return false;
        }
    }
    return true;
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

void FillHistogramFromResults(CARTA::Histogram* histogram, carta::BasicStats<float>& stats, carta::HistogramResults& results) {
    if (histogram == nullptr) {
        return;
    }

    histogram->set_num_bins(results.num_bins);
    histogram->set_bin_width(results.bin_width);
    histogram->set_first_bin_center(results.bin_center);
    *histogram->mutable_bins() = {results.histogram_bins.begin(), results.histogram_bins.end()};
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
            double nan_value = numeric_limits<double>::quiet_NaN();
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
                value = numeric_limits<double>::quiet_NaN();
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
string GetAuthTokenFromCookie(const string& header) {
    regex header_regex("carta-auth-token=(.+?)(?:;|$)");
    smatch sm;
    if (regex_search(header, sm, header_regex) && sm.size() == 2) {
        return sm[1];
    }
    return string();
}