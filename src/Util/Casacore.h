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

bool CheckFolderPaths(std::string& top_level_string, std::string& starting_string);
bool IsSubdirectory(std::string folder, std::string top_folder);
casacore::String GetResolvedFilename(const std::string& root_dir, const std::string& directory, const std::string& file);

// Determine image type from filename
inline casacore::ImageOpener::ImageTypes CasacoreImageType(const std::string& filename) {
    return casacore::ImageOpener::imageType(filename);
}

void GetSpectralCoordPreferences(
    casacore::ImageInterface<float>* image, bool& prefer_velocity, bool& optical_velocity, bool& prefer_wavelength, bool& air_wavelength);

std::string FormatBeam(const casacore::GaussianBeam& gaussian_beam);
std::string FormatQuantity(const casacore::Quantity& quantity);

#endif // CARTA_BACKEND__UTIL_CASACORE_H_
