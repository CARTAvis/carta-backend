/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <imageanalysis/ImageAnalysis/SubImageFactory.h>

#include "Logger/Logger.h"
#include "PolarizationCalculator.h"

using namespace carta;

PolarizationCalculator::PolarizationCalculator(
    std::shared_ptr<casacore::ImageInterface<float>> image, AxisRange z_range, AxisRange x_range, AxisRange y_range)
    : _image(image), _image_valid(true) {
    const auto ndim = _image->ndim();
    if (ndim < 4) {
        spdlog::error("Invalid image dimension: {}", ndim);
        _image_valid = false;
        return;
    }

    const auto& coord_sys = _image->coordinates();
    auto spectral_axis = coord_sys.spectralAxisNumber();
    if (spectral_axis < 0) {
        spectral_axis = 2; // assume spectral axis
    }
    auto stokes_axis = coord_sys.polarizationAxisNumber();
    if (stokes_axis < 0) {
        stokes_axis = 3; // assume stokes axis
    }

    std::vector<int> dir_axes = {0, 1};
    if (coord_sys.hasDirectionCoordinate()) {
        casacore::Vector<casacore::Int> tmp_axes = coord_sys.directionAxesNumbers();
        dir_axes[0] = tmp_axes[0];
        dir_axes[1] = tmp_axes[1];
    }

    const auto shape = _image->shape();
    casacore::IPosition blc(ndim, 0);
    auto trc = shape - 1;

    if (x_range.to == ALL_X) {
        x_range.from = 0;
        x_range.to = shape(dir_axes[0]) - 1;
    }

    if (x_range.from < 0 || x_range.to >= shape(dir_axes[0])) {
        spdlog::error("Invalid x range: [{}, {}]", x_range.from, x_range.to);
        _image_valid = false;
        return;
    }

    if (y_range.to == ALL_Y) {
        y_range.from = 0;
        y_range.to = shape(dir_axes[1]) - 1;
    }

    if (y_range.from < 0 || y_range.to >= shape(dir_axes[1])) {
        spdlog::error("Invalid y range: [{}, {}]", y_range.from, y_range.to);
        _image_valid = false;
        return;
    }

    if (z_range.to == ALL_Z) {
        z_range.from = 0;
        z_range.to = shape(spectral_axis) - 1;
    }

    if (z_range.from < 0 || z_range.to >= shape(spectral_axis)) {
        spdlog::error("Invalid z range: [{}, {}]", z_range.from, z_range.to);
        _image_valid = false;
        return;
    }

    // Make a region
    blc(dir_axes[0]) = x_range.from;
    trc(dir_axes[0]) = x_range.to;

    blc(dir_axes[1]) = y_range.from;
    trc(dir_axes[1]) = y_range.to;

    blc(spectral_axis) = z_range.from;
    trc(spectral_axis) = z_range.to;

    // Get stokes indices and make stokes regions
    if (coord_sys.hasPolarizationCoordinate()) {
        const auto& stokes = coord_sys.stokesCoordinate();
        int stokes_index;
        if (stokes.toPixel(stokes_index, casacore::Stokes::I)) {
            _stokes_image[I] = MakeSubImage(blc, trc, stokes_axis, stokes_index);
        }
        if (stokes.toPixel(stokes_index, casacore::Stokes::Q)) {
            _stokes_image[Q] = MakeSubImage(blc, trc, stokes_axis, stokes_index);
        }
        if (stokes.toPixel(stokes_index, casacore::Stokes::U)) {
            _stokes_image[U] = MakeSubImage(blc, trc, stokes_axis, stokes_index);
        }
        if (stokes.toPixel(stokes_index, casacore::Stokes::V)) {
            _stokes_image[V] = MakeSubImage(blc, trc, stokes_axis, stokes_index);
        }
    } else { // Assume stokes indices, I = 0, Q = 1, U = 2, V = 3
        auto stokes_axis_size = _image->shape()[stokes_axis];
        if (stokes_axis_size > 0) {
            _stokes_image[I] = MakeSubImage(blc, trc, stokes_axis, 0);
        }
        if (stokes_axis_size > 1) {
            _stokes_image[Q] = MakeSubImage(blc, trc, stokes_axis, 1);
        }
        if (stokes_axis_size > 2) {
            _stokes_image[U] = MakeSubImage(blc, trc, stokes_axis, 2);
        }
        if (stokes_axis_size > 3) {
            _stokes_image[V] = MakeSubImage(blc, trc, stokes_axis, 3);
        }
    }
}

void PolarizationCalculator::FiddleStokesCoordinate(casacore::ImageInterface<float>& image, casacore::Stokes::StokesTypes type) {
    casacore::CoordinateSystem coord_sys = image.coordinates();
    int stokes_index = coord_sys.findCoordinate(casacore::Coordinate::STOKES);
    if (stokes_index > -1) {
        casacore::Vector<int> which(1);
        which(0) = int(type);
        casacore::StokesCoordinate stokes(which);
        coord_sys.replaceCoordinate(stokes, stokes_index);
        image.setCoordinateInfo(coord_sys);
    }
}

std::shared_ptr<casacore::ImageInterface<float>> PolarizationCalculator::GetStokesImage(const StokesTypes& type) {
    return _stokes_image[type];
}

casacore::LatticeExprNode PolarizationCalculator::MakeTotalPolarizedIntensityNode() {
    casacore::LatticeExprNode lin_node = casacore::LatticeExprNode(
        casacore::pow(*_stokes_image[V], 2) + casacore::pow(*_stokes_image[U], 2) + casacore::pow(*_stokes_image[Q], 2));
    return casacore::sqrt(lin_node);
}

casacore::LatticeExprNode PolarizationCalculator::MakePolarizedIntensityNode() {
    casacore::LatticeExprNode lin_node =
        casacore::LatticeExprNode(casacore::pow(*_stokes_image[U], 2) + casacore::pow(*_stokes_image[Q], 2));
    return casacore::sqrt(lin_node);
}

std::shared_ptr<casacore::ImageInterface<float>> PolarizationCalculator::MakeSubImage(
    casacore::IPosition& blc, casacore::IPosition& trc, int axis, int pix) {
    blc(axis) = pix;
    trc(axis) = pix;
    casacore::LCSlicer slicer(blc, trc, casacore::RegionType::Abs);
    casacore::ImageRegion region(slicer);
    return std::make_shared<casacore::SubImage<float>>(*_image, region);
}

void PolarizationCalculator::SetImageStokesInfo(casacore::ImageInterface<float>& image, const StokesTypes& stokes) {
    casacore::ImageInfo info = _image->imageInfo();
    if (info.hasMultipleBeams()) {
        info.setBeams(_stokes_image[stokes]->imageInfo().getBeamSet());
    }
    image.setImageInfo(info);
}

std::shared_ptr<casacore::ImageInterface<float>> PolarizationCalculator::ComputeTotalPolarizedIntensity() {
    if ((!_image_valid)) {
        return nullptr;
    }

    auto q = GetStokesImage(Q);
    auto u = GetStokesImage(U);
    auto v = GetStokesImage(V);
    if (!q || !u || !v) {
        spdlog::error("This image does not have Stokes Q and/or U and/or V so cannot compute linear polarization");
        return nullptr;
    }

    auto node = MakeTotalPolarizedIntensityNode();
    casacore::LatticeExpr<float> lattice_expr(node);
    auto image_expr = std::make_shared<casacore::ImageExpr<float>>(lattice_expr, casacore::String("Ptotal"));
    image_expr->setUnits(_image->units());
    SetImageStokesInfo(*image_expr, Q);
    FiddleStokesCoordinate(*image_expr, casacore::Stokes::StokesTypes::Ptotal);

    return image_expr;
}

std::shared_ptr<casacore::ImageInterface<float>> PolarizationCalculator::ComputeTotalFractionalPolarizedIntensity() {
    if ((!_image_valid)) {
        return nullptr;
    }

    auto i = GetStokesImage(I);
    auto q = GetStokesImage(Q);
    auto u = GetStokesImage(U);
    auto v = GetStokesImage(V);
    if (!i || !q || !u || !v) {
        spdlog::error("This image does not have Stokes I and/or Q and/or U and/or V so cannot compute linear polarization");
        return nullptr;
    }

    auto node = 100.0 * MakeTotalPolarizedIntensityNode() / (*_stokes_image[I]);
    casacore::LatticeExpr<float> lattice_expr(node);
    auto image_expr = std::make_shared<casacore::ImageExpr<float>>(lattice_expr, casacore::String("PFtotal"));
    image_expr->setUnits(casacore::Unit("%"));
    SetImageStokesInfo(*image_expr, I);
    FiddleStokesCoordinate(*image_expr, casacore::Stokes::StokesTypes::PFtotal);

    return image_expr;
}

std::shared_ptr<casacore::ImageInterface<float>> PolarizationCalculator::ComputePolarizedIntensity() {
    if ((!_image_valid)) {
        return nullptr;
    }

    auto q = GetStokesImage(Q);
    auto u = GetStokesImage(U);
    if (!q || !u) {
        spdlog::error("This image does not have Stokes Q and/or U so cannot compute linear polarization");
        return nullptr;
    }

    auto node = MakePolarizedIntensityNode();
    casacore::LatticeExpr<float> lattice_expr(node);
    auto image_expr = std::make_shared<casacore::ImageExpr<float>>(lattice_expr, casacore::String("Plinear"));
    image_expr->setUnits(_image->units());
    SetImageStokesInfo(*image_expr, Q);
    FiddleStokesCoordinate(*image_expr, casacore::Stokes::StokesTypes::Plinear);

    return image_expr;
}

std::shared_ptr<casacore::ImageInterface<float>> PolarizationCalculator::ComputeFractionalPolarizedIntensity() {
    if ((!_image_valid)) {
        return nullptr;
    }

    auto i = GetStokesImage(I);
    auto q = GetStokesImage(Q);
    auto u = GetStokesImage(U);
    if (!i || !q || !u) {
        spdlog::error("This image does not have any one of Stokes I, Q, or U, so cannot compute linear polarization");
        return nullptr;
    }

    auto node = 100.0 * MakePolarizedIntensityNode() / (*_stokes_image[I]);
    casacore::LatticeExpr<float> lattice_expr(node);
    auto image_expr = std::make_shared<casacore::ImageExpr<float>>(lattice_expr, casacore::String("PFlinear"));
    image_expr->setUnits(casacore::Unit("%"));
    SetImageStokesInfo(*image_expr, I);
    FiddleStokesCoordinate(*image_expr, casacore::Stokes::StokesTypes::PFlinear);

    return image_expr;
}

std::shared_ptr<casacore::ImageInterface<float>> PolarizationCalculator::ComputePolarizedAngle(bool radians) {
    if ((!_image_valid)) {
        return nullptr;
    }

    auto q = GetStokesImage(Q);
    auto u = GetStokesImage(U);
    if (!q || !u) {
        spdlog::error("This image does not have Stokes Q and/or U so cannot compute linear polarization");
        return nullptr;
    }

    // Make expression
    float factor = radians ? C::pi / 180.0 : 1.0;
    casacore::LatticeExprNode node(factor * casacore::pa(*u, *q));
    casacore::LatticeExpr<float> lattice_expr(node);
    auto image_expr = std::make_shared<casacore::ImageExpr<float>>(lattice_expr, casacore::String("Pangle"));

    image_expr->setUnits(casacore::Unit(radians ? "rad" : "deg"));
    casacore::ImageInfo image_info = _image->imageInfo();
    if (image_info.hasMultipleBeams()) {
        // Since these beams can vary with stokes/polarization, they will not be copied to the output image
        image_info.removeRestoringBeam();
    }

    image_expr->setImageInfo(image_info);
    FiddleStokesCoordinate(*image_expr, casacore::Stokes::StokesTypes::Pangle);

    return image_expr;
}
