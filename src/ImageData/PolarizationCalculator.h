/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND__IMAGEDATA_POLARIZATIONCALCULATOR_H_
#define CARTA_BACKEND__IMAGEDATA_POLARIZATIONCALCULATOR_H_

#include <casacore/images/Images/ImageInterface.h>
#include <casacore/images/Images/SubImage.h>
#include <casacore/lattices/LRegions/LCSlicer.h>
#include <casacore/lattices/LRegions/RegionType.h>
#include <casacore/lattices/LatticeMath.h>
#include <casacore/lattices/LatticeMath/LatticeStatistics.h>

namespace carta {

class PolarizationCalculator {
    enum StokesTypes { I, Q, U, V };

public:
    PolarizationCalculator(std::shared_ptr<casacore::ImageInterface<float>> image, string out_name = "", bool overwrite = false);
    ~PolarizationCalculator() = default;

    // Change the casacore::Stokes casacore::Coordinate for the given complex image to be of the specified casacore::Stokes type
    void FiddleStokesCoordinate(casacore::ImageInterface<float>& image, casacore::Stokes::StokesTypes type);

    std::shared_ptr<casacore::ImageInterface<float>> GetStokesImage(const StokesTypes& type);
    casacore::LatticeExprNode MakePolarizedIntensityNode();
    void SetImageStokesInfo(casacore::ImageInterface<float>& image, const StokesTypes& stokes);

    std::shared_ptr<casacore::ImageInterface<float>> ComputePolarizedIntensity();
    std::shared_ptr<casacore::ImageInterface<float>> ComputeFractionalPolarizedIntensity();
    std::shared_ptr<casacore::ImageInterface<float>> ComputePolarizedAngle(bool radians);

private:
    // Find the casacore::Stokes in the construction image and assign pointers
    void FindStokes();

    // Make a casacore::SubImage from the construction image for the specified pixel along the specified pixel axis
    std::shared_ptr<casacore::ImageInterface<float>> MakeSubImage(casacore::IPosition& blc, casacore::IPosition& trc, int axis, int pix);

    std::shared_ptr<casacore::ImageInterface<float>> PrepareOutputImage(
        const casacore::ImageInterface<float>& image, bool drop_deg = false);

    const std::shared_ptr<const casacore::ImageInterface<float>> _image;
    string _out_name;
    bool _overwrite;
    bool _image_valid;

    // These blocks are always size 4, with IQUV in slots 0,1,2,3. If an image is IV only, it uses slots 0 and 3
    std::vector<std::shared_ptr<casacore::ImageInterface<float>>> _stokes_image =
        std::vector<std::shared_ptr<casacore::ImageInterface<float>>>(4);
    std::vector<std::shared_ptr<casacore::LatticeStatistics<float>>> _stokes_stats =
        std::vector<std::shared_ptr<casacore::LatticeStatistics<float>>>(4);
};

} // namespace carta

#endif // CARTA_BACKEND__IMAGEDATA_POLARIZATIONCALCULATOR_H_
