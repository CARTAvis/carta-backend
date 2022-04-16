/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND__IMAGEDATA_POLARIZATIONCALCULATOR_H_
#define CARTA_BACKEND__IMAGEDATA_POLARIZATIONCALCULATOR_H_

#include <casacore/images/Images/ImageExpr.h>
#include <casacore/images/Images/ImageInterface.h>
#include <casacore/images/Images/SubImage.h>
#include <casacore/lattices/LRegions/LCSlicer.h>
#include <casacore/lattices/LRegions/RegionType.h>
#include <casacore/lattices/LatticeMath.h>
#include <casacore/lattices/LatticeMath/LatticeStatistics.h>

#include "Util/Image.h"

namespace carta {

class PolarizationCalculator {
    enum StokesTypes { I, Q, U, V };

public:
    PolarizationCalculator(std::shared_ptr<casacore::ImageInterface<float>> image, AxisRange z_range = AxisRange(ALL_Z),
        AxisRange x_range = AxisRange(ALL_X), AxisRange y_range = AxisRange(ALL_Y));
    ~PolarizationCalculator() = default;

    std::shared_ptr<casacore::ImageInterface<float>> ComputeTotalPolarizedIntensity();
    std::shared_ptr<casacore::ImageInterface<float>> ComputeTotalFractionalPolarizedIntensity();
    std::shared_ptr<casacore::ImageInterface<float>> ComputePolarizedIntensity();
    std::shared_ptr<casacore::ImageInterface<float>> ComputeFractionalPolarizedIntensity();
    std::shared_ptr<casacore::ImageInterface<float>> ComputePolarizedAngle();

private:
    std::shared_ptr<casacore::ImageInterface<float>> MakeSubImage(casacore::IPosition& blc, casacore::IPosition& trc, int axis, int pix);
    std::shared_ptr<casacore::ImageInterface<float>> GetStokesImage(const StokesTypes& type);
    casacore::LatticeExprNode MakeTotalPolarizedIntensityNode();
    casacore::LatticeExprNode MakePolarizedIntensityNode();
    void SetImageStokesInfo(casacore::ImageInterface<float>& image, const StokesTypes& stokes);
    void FiddleStokesCoordinate(casacore::ImageInterface<float>& image, casacore::Stokes::StokesTypes type);

    // These blocks are always size 4, with I/Q/U/V in slots 0/1/2/3. If an image is I/V only, it uses slots 0/3
    std::vector<std::shared_ptr<casacore::ImageInterface<float>>> _stokes_images =
        std::vector<std::shared_ptr<casacore::ImageInterface<float>>>(4);

    const std::shared_ptr<const casacore::ImageInterface<float>> _image;
    bool _image_valid;
};

} // namespace carta

#endif // CARTA_BACKEND__IMAGEDATA_POLARIZATIONCALCULATOR_H_
