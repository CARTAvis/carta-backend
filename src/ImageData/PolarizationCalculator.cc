/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "PolarizationCalculator.h"
#include <imageanalysis/ImageAnalysis/SubImageFactory.h>
#include "Logger/Logger.h"

using namespace carta;

PolarizationCalculator::PolarizationCalculator(std::shared_ptr<casacore::ImageInterface<float>> image, string out_name, bool overwrite)
    : _image(image), _out_name(out_name), _overwrite(overwrite), _image_valid(true) {
    FindStokes();
}

void PolarizationCalculator::FindStokes() {
    const auto& coord_sys = _image->coordinates();
    if (!coord_sys.hasPolarizationCoordinate()) {
        spdlog::error("There is no stokes coordinate in this image.");
        _image_valid = false;
    }

    auto stokes_axis = coord_sys.polarizationAxisNumber();

    // Make stokes regions
    const auto& stokes = coord_sys.stokesCoordinate();
    const auto ndim = _image->ndim();
    const auto shape = _image->shape();
    casacore::IPosition blc(ndim, 0);
    auto trc = shape - 1;
    int pix;

    if (stokes.toPixel(pix, casacore::Stokes::I)) {
        _stokes_image[I] = MakeSubImage(blc, trc, stokes_axis, pix);
    }
    if (stokes.toPixel(pix, casacore::Stokes::Q)) {
        _stokes_image[Q] = MakeSubImage(blc, trc, stokes_axis, pix);
    }
    if (stokes.toPixel(pix, casacore::Stokes::U)) {
        _stokes_image[U] = MakeSubImage(blc, trc, stokes_axis, pix);
    }
    if (stokes.toPixel(pix, casacore::Stokes::V)) {
        _stokes_image[V] = MakeSubImage(blc, trc, stokes_axis, pix);
    }

    if ((_stokes_image[Q] && !_stokes_image[U]) || (!_stokes_image[Q] && _stokes_image[U])) {
        spdlog::error("Stokes coordinate has only one Q or U type.");
        _image_valid = false;
    }
}

void PolarizationCalculator::FiddleStokesCoordinate(casacore::ImageInterface<float>& image, casacore::Stokes::StokesTypes type) {
    casacore::CoordinateSystem coord_sys = image.coordinates();
    int after_coord(-1);
    int stokes_index = coord_sys.findCoordinate(casacore::Coordinate::STOKES, after_coord);
    casacore::Vector<int> which(1);
    which(0) = int(type);
    casacore::StokesCoordinate stokes(which);
    coord_sys.replaceCoordinate(stokes, stokes_index);
    image.setCoordinateInfo(coord_sys);
}

std::shared_ptr<casacore::ImageInterface<float>> PolarizationCalculator::GetStokesImage(const StokesTypes& type) {
    return _stokes_image[type];
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

std::shared_ptr<casacore::ImageInterface<float>> PolarizationCalculator::PrepareOutputImage(
    const casacore::ImageInterface<float>& image, bool drop_deg) {
    static const casacore::Record empty;
    static const casacore::String empty_string;
    bool list(true);
    bool extend_mask(false);
    bool attach_mask(false);
    auto out_image = casa::SubImageFactory<float>::createImage(
        image, _out_name, empty, empty_string, drop_deg, _overwrite, list, extend_mask, attach_mask);
    return out_image;
}

std::shared_ptr<casacore::ImageInterface<float>> PolarizationCalculator::ComputePolarizationIntensity() {
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
    casacore::ImageExpr<float> image_expr(lattice_expr, casacore::String("LinearlyPolarizedIntensity"));
    image_expr.setUnits(_image->units());
    SetImageStokesInfo(image_expr, Q);

    // Fiddle Stokes coordinate
    FiddleStokesCoordinate(image_expr, casacore::Stokes::Plinear);

    return PrepareOutputImage(image_expr);
}

std::shared_ptr<casacore::ImageInterface<float>> PolarizationCalculator::ComputeFractionalPolarizationIntensity() {
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

    auto node = MakePolarizedIntensityNode();
    node = node / *_stokes_image[I];
    casacore::LatticeExpr<float> lattice_expr(node);
    casacore::ImageExpr<float> image_expr(lattice_expr, casacore::String("LinearlyFractionalPolarizedIntensity"));
    image_expr.setUnits(_image->units());
    SetImageStokesInfo(image_expr, I);

    // Fiddle Stokes coordinate
    FiddleStokesCoordinate(image_expr, casacore::Stokes::Plinear);

    return PrepareOutputImage(image_expr);
}

std::shared_ptr<casacore::ImageInterface<float>> PolarizationCalculator::ComputePolarizationAngle(bool radians) {
    if ((!_image_valid)) {
        return nullptr;
    }

    auto q = GetStokesImage(Q);
    auto u = GetStokesImage(U);
    if (!q || !u) {
        spdlog::error("This image does not have Stokes Q and/or U so cannot compute linear polarization");
        return nullptr;
    }

    // Make expression. LEL function "pa" returns degrees
    float factor = radians ? C::pi / 180.0 : 1.0;
    casacore::LatticeExprNode node(factor * casacore::pa(*u, *q));
    casacore::LatticeExpr<float> lattice_expr(node);
    casacore::ImageExpr<float> image_expr(lattice_expr, casacore::String("LinearlyPolarizedPositionAngle"));

    image_expr.setUnits(casacore::Unit(radians ? "rad" : "deg"));
    casacore::ImageInfo image_info = _image->imageInfo();
    if (image_info.hasMultipleBeams()) {
        spdlog::warn(
            "The input image has multiple beams. Because these beams can vary with stokes/polarization, they will not be copied to the "
            "output image");
        image_info.removeRestoringBeam();
    }

    image_expr.setImageInfo(image_info);
    FiddleStokesCoordinate(image_expr, casacore::Stokes::Pangle);

    return PrepareOutputImage(image_expr);
}
