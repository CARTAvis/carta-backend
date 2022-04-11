/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND__UTIL_CASACORE_H_
#define CARTA_BACKEND__UTIL_CASACORE_H_

#include <casacore/images/Images/ImageInterface.h>
#include <casacore/images/Images/ImageOpener.h>
#include <casacore/scimath/Mathematics/GaussianBeam.h>

struct CoordinateAxes {
    casacore::IPosition image_shape;
    std::vector<int> xy_axes;
    int z_axis;
    int spectral_axis;
    int stokes_axis;
    size_t width;
    size_t height;
    size_t depth;
    size_t num_stokes;

    CoordinateAxes() : xy_axes({0, 1}), z_axis(-1), spectral_axis(-1), stokes_axis(-1), width(0), height(0), depth(1), num_stokes(1) {}
    CoordinateAxes(const casacore::IPosition& shape, const std::vector<int>& xy_axis, int z_axis, int sp_axis, int st_axis)
        : image_shape(shape), xy_axes(xy_axis), spectral_axis(sp_axis), stokes_axis(st_axis), z_axis(z_axis) {
        width = image_shape(xy_axes[0]);
        height = image_shape(xy_axes[1]);
        depth = z_axis >= 0 ? image_shape(z_axis) : 1;
        num_stokes = stokes_axis >= 0 ? image_shape(stokes_axis) : 1;
    }
    void operator=(CoordinateAxes& other) {
        image_shape = other.image_shape;
        xy_axes = other.xy_axes;
        z_axis = other.z_axis;
        spectral_axis = other.spectral_axis;
        stokes_axis = other.stokes_axis;
        width = other.width;
        height = other.height;
        depth = other.depth;
        num_stokes = other.num_stokes;
    }
};

bool CheckFolderPaths(std::string& top_level_string, std::string& starting_string);
bool IsSubdirectory(std::string folder, std::string top_folder);
casacore::String GetResolvedFilename(const std::string& root_dir, const std::string& directory, const std::string& file);

// Determine image type from filename
inline casacore::ImageOpener::ImageTypes CasacoreImageType(const std::string& filename) {
    return casacore::ImageOpener::imageType(filename);
}

void GetSpectralCoordSettings(
    casacore::ImageInterface<float>* image, bool& prefer_velocity, bool& optical_velocity, bool& prefer_wavelength, bool& air_wavelength);

std::string FormatBeam(const casacore::GaussianBeam& gaussian_beam);
std::string FormatQuantity(const casacore::Quantity& quantity);

// Determine axes information
CoordinateAxes FindCoordinateAxes(const casacore::CoordinateSystem& coord_sys, const casacore::IPosition& image_shape);
std::vector<int> GetRenderAxes(const casacore::CoordinateSystem& coord_sys, const casacore::IPosition& image_shape);

#endif // CARTA_BACKEND__UTIL_CASACORE_H_
