/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "PolarizationCalculator.h"
#include "Logger/Logger.h"

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
    std::vector<int> dir_axes = {0, 1}; // By default, the spatial axes numbers are 0 and 1
    if (coord_sys.hasDirectionCoordinate()) {
        casacore::Vector<casacore::Int> tmp_axes = coord_sys.directionAxesNumbers();
        dir_axes[0] = tmp_axes[0];
        dir_axes[1] = tmp_axes[1];
    }

    auto spectral_axis = coord_sys.spectralAxisNumber();
    if (spectral_axis < 0) {
        spectral_axis = 2; // Assume spectral axis number = 2
    }

    auto stokes_axis = coord_sys.polarizationAxisNumber();
    if (stokes_axis < 0) {
        stokes_axis = 3; // Assume stokes axis number = 3
    }

    const auto shape = _image->shape();
    casacore::IPosition blc(ndim, 0);
    casacore::IPosition trc = shape - 1;

    if (x_range.to == ALL_X) {
        x_range.from = 0;
        x_range.to = shape(dir_axes[0]) - 1;
    }

    if (y_range.to == ALL_Y) {
        y_range.from = 0;
        y_range.to = shape(dir_axes[1]) - 1;
    }

    if (z_range.to == ALL_Z) {
        z_range.from = 0;
        z_range.to = shape(spectral_axis) - 1;
    }

    if (x_range.from < 0 || x_range.to >= shape(dir_axes[0]) || y_range.from < 0 || y_range.to >= shape(dir_axes[1]) || z_range.from < 0 ||
        z_range.to >= shape(spectral_axis)) {
        spdlog::error("Invalid selection region.");
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
            _stokes_images[I] = MakeSubImage(blc, trc, stokes_axis, stokes_index);
        }
        if (stokes.toPixel(stokes_index, casacore::Stokes::Q)) {
            _stokes_images[Q] = MakeSubImage(blc, trc, stokes_axis, stokes_index);
        }
        if (stokes.toPixel(stokes_index, casacore::Stokes::U)) {
            _stokes_images[U] = MakeSubImage(blc, trc, stokes_axis, stokes_index);
        }
        if (stokes.toPixel(stokes_index, casacore::Stokes::V)) {
            _stokes_images[V] = MakeSubImage(blc, trc, stokes_axis, stokes_index);
        }
    } else { // Assume stokes indices: I = 0, Q = 1, U = 2, and V = 3
        auto stokes_axis_size = _image->shape()[stokes_axis];
        if (stokes_axis_size > 0) {
            _stokes_images[I] = MakeSubImage(blc, trc, stokes_axis, 0);
        }
        if (stokes_axis_size > 1) {
            _stokes_images[Q] = MakeSubImage(blc, trc, stokes_axis, 1);
        }
        if (stokes_axis_size > 2) {
            _stokes_images[U] = MakeSubImage(blc, trc, stokes_axis, 2);
        }
        if (stokes_axis_size > 3) {
            _stokes_images[V] = MakeSubImage(blc, trc, stokes_axis, 3);
        }
    }
}

void PolarizationCalculator::FiddleStokesCoordinate(casacore::ImageInterface<float>& image, casacore::Stokes::StokesTypes type) {
    casacore::CoordinateSystem coord_sys = image.coordinates();
    int stokes_index = coord_sys.findCoordinate(casacore::Coordinate::STOKES);
    if (stokes_index > -1) {
        casacore::Vector<int> which(1);
        which(0) = (int)type;
        casacore::StokesCoordinate stokes(which);
        coord_sys.replaceCoordinate(stokes, stokes_index);
        image.setCoordinateInfo(coord_sys);
    }
}

casacore::LatticeExprNode PolarizationCalculator::MakeTotalPolarizedIntensityNode() {
    casacore::LatticeExprNode lin_node = casacore::LatticeExprNode(
        casacore::pow(*_stokes_images[V], 2) + casacore::pow(*_stokes_images[U], 2) + casacore::pow(*_stokes_images[Q], 2));
    return casacore::sqrt(lin_node);
}

casacore::LatticeExprNode PolarizationCalculator::MakePolarizedIntensityNode() {
    casacore::LatticeExprNode lin_node =
        casacore::LatticeExprNode(casacore::pow(*_stokes_images[U], 2) + casacore::pow(*_stokes_images[Q], 2));
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
        info.setBeams(_stokes_images[stokes]->imageInfo().getBeamSet());
    }
    image.setImageInfo(info);
}

std::shared_ptr<casacore::ImageInterface<float>> PolarizationCalculator::ComputeTotalPolarizedIntensity() {
    if (!_image_valid) {
        return nullptr;
    }

    if (!_stokes_images[Q] || !_stokes_images[U] || !_stokes_images[V]) {
        spdlog::error("This image lacks stokes Q, U, or V. Cannot compute total polarized intensity");
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
    if (!_image_valid) {
        return nullptr;
    }

    if (!_stokes_images[I] || !_stokes_images[Q] || !_stokes_images[U] || !_stokes_images[V]) {
        spdlog::error("This image lacks stokes I, Q, U, or V. Cannot compute total fractional polarized intensity");
        return nullptr;
    }

    auto node = 100.0 * MakeTotalPolarizedIntensityNode() / (*_stokes_images[I]);
    casacore::LatticeExpr<float> lattice_expr(node);
    auto image_expr = std::make_shared<casacore::ImageExpr<float>>(lattice_expr, casacore::String("PFtotal"));
    image_expr->setUnits(casacore::Unit("%"));
    SetImageStokesInfo(*image_expr, I);
    FiddleStokesCoordinate(*image_expr, casacore::Stokes::StokesTypes::PFtotal);
    return image_expr;
}

std::shared_ptr<casacore::ImageInterface<float>> PolarizationCalculator::ComputePolarizedIntensity() {
    if (!_image_valid) {
        return nullptr;
    }

    if (!_stokes_images[Q] || !_stokes_images[U]) {
        spdlog::error("This image lacks stokes Q or U. Cannot compute polarized intensity");
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
    if (!_image_valid) {
        return nullptr;
    }

    if (!_stokes_images[I] || !_stokes_images[Q] || !_stokes_images[U]) {
        spdlog::error("This image lacks stokes I, Q, or U. Cannot compute fractional polarized intensity");
        return nullptr;
    }

    auto node = 100.0 * MakePolarizedIntensityNode() / (*_stokes_images[I]);
    casacore::LatticeExpr<float> lattice_expr(node);
    auto image_expr = std::make_shared<casacore::ImageExpr<float>>(lattice_expr, casacore::String("PFlinear"));
    image_expr->setUnits(casacore::Unit("%"));
    SetImageStokesInfo(*image_expr, I);
    FiddleStokesCoordinate(*image_expr, casacore::Stokes::StokesTypes::PFlinear);
    return image_expr;
}

std::shared_ptr<casacore::ImageInterface<float>> PolarizationCalculator::ComputePolarizedAngle() {
    if (!_image_valid) {
        return nullptr;
    }

    if (!_stokes_images[Q] || !_stokes_images[U]) {
        spdlog::error("This image lacks stokes Q or U. Cannot compute polarized angle");
        return nullptr;
    }

    casacore::LatticeExprNode node(casacore::pa(*_stokes_images[U], *_stokes_images[Q]));
    casacore::LatticeExpr<float> lattice_expr(node);
    auto image_expr = std::make_shared<casacore::ImageExpr<float>>(lattice_expr, casacore::String("Pangle"));
    image_expr->setUnits(casacore::Unit("deg"));
    casacore::ImageInfo image_info = _image->imageInfo();

    // Since multiple beams can vary with stokes/polarization, they will not be copied to the output image
    if (image_info.hasMultipleBeams()) {
        image_info.removeRestoringBeam();
    }

    image_expr->setImageInfo(image_info);
    FiddleStokesCoordinate(*image_expr, casacore::Stokes::StokesTypes::Pangle);
    return image_expr;
}
