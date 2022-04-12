/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "Casacore.h"

#include <casacore/casa/OS/File.h>

#include "ImageData/CartaMiriadImage.h"
#include "Logger/Logger.h"

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

void GetSpectralCoordSettings(
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

std::string FormatBeam(const casacore::GaussianBeam& gaussian_beam) {
    std::string result;
    result += fmt::format("major: {:.6f} {} ", gaussian_beam.getMajor().getValue(), gaussian_beam.getMajor().getUnit());
    result += fmt::format("minor: {:.6f} {} ", gaussian_beam.getMinor().getValue(), gaussian_beam.getMinor().getUnit());
    result += fmt::format("pa: {:.6f} {}", gaussian_beam.getPA().getValue(), gaussian_beam.getPA().getUnit());
    return result;
}

std::string FormatQuantity(const casacore::Quantity& quantity) {
    return fmt::format("{:.6f} {}", quantity.getValue(), quantity.getUnit());
}

CoordinateAxes FindCoordinateAxes(const casacore::CoordinateSystem& coord_sys, const casacore::IPosition& image_shape) {
    std::vector<int> xy_axes = GetRenderAxes(coord_sys, image_shape);
    int spectral_axis(coord_sys.spectralAxisNumber());
    int stokes_axis(coord_sys.polarizationAxisNumber());
    int z_axis(-1);

    bool no_spectral(spectral_axis < 0), no_stokes(stokes_axis < 0);
    size_t num_axes(image_shape.size());

    if (no_spectral && no_stokes && (num_axes > 2)) {
        // Cope with incomplete/invalid headers for 3D, 4D images
        if ((no_spectral && no_stokes) && (num_axes == 3)) {
            // assume third is spectral with no stokes
            spectral_axis = 2;
        }

        if ((no_spectral || no_stokes) && (num_axes == 4)) {
            if (no_spectral && !no_stokes) { // stokes is known
                spectral_axis = (stokes_axis == 3 ? 2 : 3);
            } else if (!no_spectral && no_stokes) { // spectral is known
                stokes_axis = (spectral_axis == 3 ? 2 : 3);
            } else { // neither is known
                // guess by shape (max 4 stokes)
                if (image_shape(2) > 4) {
                    spectral_axis = 2;
                    stokes_axis = 3;
                } else if (image_shape(3) > 4) {
                    spectral_axis = 3;
                    stokes_axis = 2;
                }

                if ((spectral_axis < 0) && (stokes_axis < 0)) {
                    // could not guess, assume [spectral, stokes]
                    spectral_axis = 2;
                    stokes_axis = 3;
                }
            }
        }
    }

    // Set z axis: depth axis that is not stokes
    for (size_t i = 0; i < num_axes; ++i) {
        if ((i != xy_axes[0]) && (i != xy_axes[1]) && (i != stokes_axis)) {
            z_axis = i;
            break;
        }
    }

    return CoordinateAxes(image_shape, xy_axes, z_axis, spectral_axis, stokes_axis);
}

std::vector<int> GetRenderAxes(const casacore::CoordinateSystem& coord_sys, const casacore::IPosition& image_shape) {
    // Determine which axes will be rendered
    std::vector<int> axes = {0, 1};

    if (image_shape.size() > 2) {
        // Normally, use direction axes
        if (coord_sys.hasDirectionCoordinate()) {
            casacore::Vector<casacore::Int> dir_axes = coord_sys.directionAxesNumbers();
            axes[0] = dir_axes[0];
            axes[1] = dir_axes[1];
        } else if (coord_sys.hasLinearCoordinate()) {
            // Check for PV image: [Linear, Spectral] axes
            // Returns -1 if no spectral axis
            int spectral_axis = coord_sys.spectralAxisNumber();

            if (spectral_axis >= 0) {
                // Find valid (not -1) linear axes
                std::vector<int> valid_axes;
                casacore::Vector<casacore::Int> lin_axes = coord_sys.linearAxesNumbers();
                for (auto axis : lin_axes) {
                    if (axis >= 0) {
                        valid_axes.push_back(axis);
                    }
                }

                // One linear + spectral axis = pV image
                if (valid_axes.size() == 1) {
                    valid_axes.push_back(spectral_axis);
                    axes = valid_axes;
                }
            }
        }
    }

    return axes;
}
